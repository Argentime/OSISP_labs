#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>
#include <sched.h>
#include <stdint.h>

#define MAX_THREADS 100
#define INITIAL_QUEUE_SIZE 10

typedef struct
{
    char type;
    unsigned short hash;
    unsigned char size;
    char data[];
} Message;

typedef struct
{
    Message **buffer;
    int head, tail;
    int produced, consumed;
    int queue_size;
    int producers, consumers;
    sem_t sem_fill, sem_empty;
    pthread_mutex_t mutex;
    int resize_pending;
} Queue;

Queue queue;
pthread_t producers[MAX_THREADS], consumers[MAX_THREADS];
int p_count = 0, c_count = 0;
volatile int running = 1;

unsigned short compute_hash(const Message *msg)
{
    uint16_t hash = 0;
    uint8_t *ptr = (uint8_t *)&msg->type; // Указатель на первый байт структуры

    // XOR для type
    hash ^= *ptr++;

    // Пропускаем hash (2 байта)
    ptr += sizeof(unsigned short);

    // XOR для size
    hash ^= *ptr++;

    // XOR для data (msg->size байтов)
    for (int i = 0; i < msg->size; i++)
    {
        hash ^= ptr[i];
    }
    return hash;
}

void *producer(void *arg)
{
    unsigned int seed = (unsigned int)pthread_self();
    while (running)
    {
        sem_wait(&queue.sem_fill);
        pthread_mutex_lock(&queue.mutex);

        if (queue.resize_pending)
        {
            pthread_mutex_unlock(&queue.mutex);
            sem_post(&queue.sem_fill);
            sched_yield();
            continue;
        }

        int size = (rand_r(&seed) % 256) + 1;
        int padded_size = ((size + 3) / 4) * 4;
        Message *msg = malloc(sizeof(Message) + padded_size);
        msg->type = 'P';
        msg->size = size - 1;
        for (int i = 0; i < size - 1; i++)
        {
            msg->data[i] = rand_r(&seed) % 256;
        }
        msg->hash = 0;
        msg->hash = compute_hash(msg);

        queue.buffer[queue.tail] = msg;
        queue.tail = (queue.tail + 1) % queue.queue_size;
        queue.produced++;

        printf("[Producer %lu] Produced: %d\n", pthread_self(), queue.produced);
        fflush(stdout);

        pthread_mutex_unlock(&queue.mutex);
        sem_post(&queue.sem_empty);
        sleep(1);
    }
    return NULL;
}

void *consumer(void *arg)
{
    while (running)
    {
        sem_wait(&queue.sem_empty);
        pthread_mutex_lock(&queue.mutex);

        if (queue.produced == queue.consumed)
        {
            pthread_mutex_unlock(&queue.mutex);
            sem_post(&queue.sem_empty);
            sched_yield();
            continue;
        }

        Message *msg = queue.buffer[queue.head];
        queue.head = (queue.head + 1) % queue.queue_size;
        queue.consumed++;

        pthread_mutex_unlock(&queue.mutex);
        sem_post(&queue.sem_fill);

        unsigned short computed_hash = compute_hash(msg);
        int valid = (computed_hash == msg->hash);
        printf("[Consumer %lu] Consumed: %d, Hash valid: %s\n",
               pthread_self(), queue.consumed, valid ? "yes" : "no");
        fflush(stdout);

        free(msg);
        sleep(1);
    }
    return NULL;
}

void resize_queue(int new_size)
{
    pthread_mutex_lock(&queue.mutex);
    if (new_size == queue.queue_size)
    {
        pthread_mutex_unlock(&queue.mutex);
        return;
    }

    Message **new_buffer = calloc(new_size, sizeof(Message *));
    int count = queue.produced - queue.consumed;
    int new_head = 0, new_tail = 0;

    if (count > new_size)
    {
        count = new_size;
    }

    for (int i = 0; i < count; i++)
    {
        new_buffer[new_tail] = queue.buffer[(queue.head + i) % queue.queue_size];
        new_tail++;
    }

    free(queue.buffer);
    queue.buffer = new_buffer;
    queue.head = 0;
    queue.tail = new_tail;
    queue.queue_size = new_size;
    queue.resize_pending = (new_size < queue.queue_size);

    sem_destroy(&queue.sem_fill);
    sem_destroy(&queue.sem_empty);
    sem_init(&queue.sem_fill, 0, new_size - count);
    sem_init(&queue.sem_empty, 0, count);

    pthread_mutex_unlock(&queue.mutex);
}

int main()
{
    queue.queue_size = INITIAL_QUEUE_SIZE;
    queue.buffer = calloc(queue.queue_size, sizeof(Message *));
    queue.head = queue.tail = queue.produced = queue.consumed = 0;
    queue.producers = queue.consumers = 0;
    queue.resize_pending = 0;
    sem_init(&queue.sem_fill, 0, queue.queue_size);
    sem_init(&queue.sem_empty, 0, 0);
    pthread_mutex_init(&queue.mutex, NULL);

    printf("[Main] Enter 'p', 'c', 'k', 's', '+', '-', or 'q': ");
    char input;
    while ((input = getchar()) != EOF)
    {
        if (input == '\n')
            continue;

        if (input == 'q')
        {
            running = 0;
            for (int i = 0; i < p_count; i++)
                pthread_cancel(producers[i]);
            for (int i = 0; i < c_count; i++)
                pthread_cancel(consumers[i]);
            for (int i = 0; i < p_count; i++)
                pthread_join(producers[i], NULL);
            for (int i = 0; i < c_count; i++)
                pthread_join(consumers[i], NULL);
            break;
        }

        if (input == 'p' && p_count < MAX_THREADS)
        {
            pthread_create(&producers[p_count++], NULL, producer, NULL);
            queue.producers++;
            printf("[Main] Created producer %lu\n", producers[p_count - 1]);
        }

        if (input == 'c' && c_count < MAX_THREADS)
        {
            pthread_create(&consumers[c_count++], NULL, consumer, NULL);
            queue.consumers++;
            printf("[Main] Created consumer %lu\n", consumers[c_count - 1]);
        }

        if (input == 'k')
        {
            for (int i = 0; i < p_count; i++)
                pthread_cancel(producers[i]);
            for (int i = 0; i < c_count; i++)
                pthread_cancel(consumers[i]);
            for (int i = 0; i < p_count; i++)
                pthread_join(producers[i], NULL);
            for (int i = 0; i < c_count; i++)
                pthread_join(consumers[i], NULL);
            p_count = c_count = 0;
            queue.producers = queue.consumers = 0;
            printf("[Main] All threads terminated\n");
        }

        if (input == 's')
        {
            pthread_mutex_lock(&queue.mutex);
            printf("[Main] Queue: size=%d, occupied=%d, free=%d, producers=%d, consumers=%d\n",
                   queue.queue_size, queue.produced - queue.consumed,
                   queue.queue_size - (queue.produced - queue.consumed),
                   queue.producers, queue.consumers);
            pthread_mutex_unlock(&queue.mutex);
        }

        if (input == '+')
        {
            resize_queue(queue.queue_size + 1);
            printf("[Main] Queue size increased to %d\n", queue.queue_size);
        }

        if (input == '-')
        {
            if (queue.queue_size > 1)
            {
                queue.resize_pending = 1;
                resize_queue(queue.queue_size - 1);
                queue.resize_pending = 0;
                printf("[Main] Queue size decreased to %d\n", queue.queue_size);
            }
            else
            {
                printf("[Main] Cannot decrease queue size below 1\n");
            }
        }

        printf("[Main] Enter 'p', 'c', 'k', 's', '+', '-', or 'q': ");
    }

    for (int i = 0; i < queue.queue_size; i++)
    {
        if (queue.buffer[i])
            free(queue.buffer[i]);
    }
    free(queue.buffer);
    sem_destroy(&queue.sem_fill);
    sem_destroy(&queue.sem_empty);
    pthread_mutex_destroy(&queue.mutex);
    return 0;
}