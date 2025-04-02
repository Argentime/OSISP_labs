#define _POSIX_C_SOURCE 200809L
#include "common.h" // Include the common header
#include <time.h>   // For seeding rand_r

// Define the global running flag (declared extern in common.h)
volatile sig_atomic_t running = 1;

// --- Signal Handler ---
void signal_handler(int sig)
{
    running = 0;
}

int main()
{
    // --- Setup Signal Handling ---
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);

    // --- IPC Initialization ---
    key_t key;
    int shmid = -1;
    int semid = -1;
    Queue *queue = (Queue *)-1;

    // Generate IPC Key (using path from common.h)
    key = ftok(IPC_KEY_PATH, IPC_KEY_ID);
    if (key == -1)
    {
        perror("ftok - Ensure '" IPC_KEY_PATH "' exists");
        exit(EXIT_FAILURE);
    }

    // Get Shared Memory Segment ID (don't create, assume main creates it)
    shmid = shmget(key, sizeof(Queue), 0666);
    if (shmid == -1)
    {
        perror("shmget (producer) - Did main start and create the segment?");
        exit(EXIT_FAILURE);
    }

    // Attach Shared Memory Segment
    queue = shmat(shmid, NULL, 0);
    if (queue == (void *)-1)
    {
        perror("shmat (producer)");
        exit(EXIT_FAILURE);
    }

    // Get Semaphore Set ID (don't create)
    semid = semget(key, 3, 0666);
    if (semid == -1)
    {
        perror("semget (producer) - Did main start and create the semaphores?");
        shmdt(queue); // Detach memory before exiting
        exit(EXIT_FAILURE);
    }

    printf("[Producer %d] Started. Attached to SHM id %d, SEM id %d.\n", getpid(), shmid, semid);
    fflush(stdout);

    // Seed for random data generation
    unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();

    // --- Main Production Loop ---
    while (running)
    {
        printf("[Producer %d] Waiting for empty slot (EMPTY_SLOTS = %d)...\n",
               getpid(), semctl(semid, SEM_EMPTY_SLOTS, GETVAL));
        fflush(stdout);

        // 1. Wait for an empty slot (P operation on SEM_EMPTY_SLOTS)
        sem_op(semid, SEM_EMPTY_SLOTS, -1);

        // Check if we should terminate after waking up
        if (!running)
            break;

        printf("[Producer %d] Slot available. Waiting for mutex (MUTEX = %d)...\n",
               getpid(), semctl(semid, SEM_MUTEX, GETVAL));
        fflush(stdout);

        // 2. Acquire Mutex (P operation on SEM_MUTEX)
        sem_op(semid, SEM_MUTEX, -1);
        printf("[Producer %d] Mutex acquired.\n", getpid());
        fflush(stdout);

        // --- Critical Section ---
        // Now we have exclusive access to the queue's tail pointer

        // Check if running flag changed while waiting for mutex
        if (!running)
        {
            printf("[Producer %d] Terminating inside critical section.\n", getpid());
            fflush(stdout);
            sem_op(semid, SEM_MUTEX, 1); // Release mutex
            // Must put EMPTY_SLOTS back since we didn't produce
            sem_op(semid, SEM_EMPTY_SLOTS, 1);
            break;
        }

        // 3. Prepare message in local variables first
        MessageHeader header;
        char data_buffer[MAX_MESSAGE_DATA_SIZE];

        header.type = 'D'; // Example type
        // Generate random data size (1 to MAX_MESSAGE_DATA_SIZE bytes)
        // rand_r is thread-safe but maybe overkill here, rand() is simpler if single-threaded producer
        header.size = (rand_r(&seed) % MAX_MESSAGE_DATA_SIZE) + 1;

        // Generate random data
        for (int i = 0; i < header.size; ++i)
        {
            data_buffer[i] = rand_r(&seed) % 256;
        }

        // 4. Compute hash based on local header and data
        header.hash = compute_hash(&header, data_buffer);

        // 5. Copy header and data into the shared memory buffer slot
        char *target_slot = queue->buffer[queue->tail];
        memcpy(target_slot, &header, sizeof(MessageHeader));                   // Copy header
        memcpy(target_slot + sizeof(MessageHeader), data_buffer, header.size); // Copy data right after header

        // 6. Update tail pointer
        int current_tail = queue->tail; // For logging
        queue->tail = (queue->tail + 1) % QUEUE_SIZE;

        printf("[Producer %d] Produced message (Size: %d, Hash: 0x%X) in slot %d. New tail: %d.\n",
               getpid(), header.size, header.hash, current_tail, queue->tail);
        fflush(stdout);

        // --- End of Critical Section ---

        // 7. Release Mutex (V operation on SEM_MUTEX)
        sem_op(semid, SEM_MUTEX, 1);
        printf("[Producer %d] Mutex released.\n", getpid());
        fflush(stdout);

        // 8. Signal a filled slot (V operation on SEM_FILLED_SLOTS)
        sem_op(semid, SEM_FILLED_SLOTS, 1);
        printf("[Producer %d] Signaled filled slot (FILLED_SLOTS = %d).\n",
               getpid(), semctl(semid, SEM_FILLED_SLOTS, GETVAL));
        fflush(stdout);

        // Optional delay
        // struct timespec ts = {0, 100 * 1000000}; // 100 ms
        // nanosleep(&ts, NULL);
        sleep(1); // Simpler 1 second delay

    } // End while(running)

    // --- Cleanup ---
    printf("[Producer %d] Termination signal received or loop ended. Detaching shared memory.\n", getpid());
    fflush(stdout);

    // Detach shared memory segment
    if (queue != (void *)-1)
    {
        if (shmdt(queue) == -1)
        {
            perror("shmdt (producer)");
        }
        else
        {
            printf("[Producer %d] Shared memory detached.\n", getpid());
        }
    }

    printf("[Producer %d] Exiting.\n", getpid());
    fflush(stdout);
    return 0;
}