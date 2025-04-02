#define _POSIX_C_SOURCE 200809L
#include "common.h" // Include the common header

// Define the global running flag (declared extern in common.h)
volatile sig_atomic_t running = 1;

// --- Signal Handler ---
void signal_handler(int sig)
{
    running = 0;
}

// --- Main Function ---
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

    // Get Shared Memory Segment ID (don't create)
    shmid = shmget(key, sizeof(Queue), 0666);
    if (shmid == -1)
    {
        perror("shmget (consumer) - Did main/producer start and create the segment?");
        exit(EXIT_FAILURE);
    }

    // Attach Shared Memory Segment
    queue = shmat(shmid, NULL, 0);
    if (queue == (void *)-1)
    {
        perror("shmat (consumer)");
        exit(EXIT_FAILURE);
    }

    // Get Semaphore Set ID (don't create)
    semid = semget(key, 3, 0666);
    if (semid == -1)
    {
        perror("semget (consumer) - Did main/producer start and create the semaphores?");
        shmdt(queue);
        exit(EXIT_FAILURE);
    }

    printf("[Consumer %d] Started. Attached to SHM id %d, SEM id %d.\n", getpid(), shmid, semid);
    printf("[Consumer %d] Initial Semaphores: EMPTY_SLOTS=%d, FILLED_SLOTS=%d, MUTEX=%d\n",
           getpid(), semctl(semid, SEM_EMPTY_SLOTS, GETVAL),
           semctl(semid, SEM_FILLED_SLOTS, GETVAL), semctl(semid, SEM_MUTEX, GETVAL));
    fflush(stdout);

    // --- Main Consumption Loop ---
    while (running)
    {
        printf("[Consumer %d] Waiting for a message (FILLED_SLOTS = %d)...\n",
               getpid(), semctl(semid, SEM_FILLED_SLOTS, GETVAL));
        fflush(stdout);

        // 1. Wait for a filled slot (P operation on FILLED_SLOTS)
        sem_op(semid, SEM_FILLED_SLOTS, -1);

        // Check if we should terminate after waking up
        if (!running)
            break;

        printf("[Consumer %d] Message available. Waiting for mutex (MUTEX = %d)...\n",
               getpid(), semctl(semid, SEM_MUTEX, GETVAL));
        fflush(stdout);

        // 2. Acquire Mutex (P operation on SEM_MUTEX)
        sem_op(semid, SEM_MUTEX, -1);
        printf("[Consumer %d] Mutex acquired.\n", getpid());
        fflush(stdout);

        // --- Critical Section ---
        // Now we have exclusive access to the queue's head pointer

        // Check if running flag changed while waiting for mutex
        if (!running)
        {
            printf("[Consumer %d] Terminating inside critical section.\n", getpid());
            fflush(stdout);
            sem_op(semid, SEM_MUTEX, 1); // Release mutex
            // Must put FILLED_SLOTS back since we didn't consume
            sem_op(semid, SEM_FILLED_SLOTS, 1);
            break;
        }

        // 3. Read message from shared memory buffer into local memory
        // Copy the entire potential message slot first.
        char local_message_buffer[MAX_MESSAGE_SIZE];
        memcpy(local_message_buffer, queue->buffer[queue->head], MAX_MESSAGE_SIZE);

        // Get pointers to header and data within the local buffer
        MessageHeader *local_header = (MessageHeader *)local_message_buffer;
        char *local_data = local_message_buffer + sizeof(MessageHeader);

        // Validate size before using it further (important!)
        if (local_header->size > MAX_MESSAGE_DATA_SIZE)
        {
            fprintf(stderr, "[Consumer %d] CORRUPTED MESSAGE: Invalid size %d in slot %d. Skipping.\n",
                    getpid(), local_header->size, queue->head);
            // Update head even for corrupted message to advance past it
            queue->head = (queue->head + 1) % QUEUE_SIZE;
            sem_op(semid, SEM_MUTEX, 1);       // Release mutex
            sem_op(semid, SEM_EMPTY_SLOTS, 1); // Signal slot is now 'empty' (even though corrupted)
            continue;                          // Skip processing this corrupted message
        }

        // 4. Update head pointer *after* successfully reading/copying
        int current_head = queue->head; // For logging
        queue->head = (queue->head + 1) % QUEUE_SIZE;

        printf("[Consumer %d] Consumed message from slot %d. New head: %d.\n",
               getpid(), current_head, queue->head);
        fflush(stdout);

        // --- End of Critical Section ---

        // 5. Release Mutex (V operation on SEM_MUTEX)
        sem_op(semid, SEM_MUTEX, 1);
        printf("[Consumer %d] Mutex released.\n", getpid());
        fflush(stdout);

        // 6. Signal an empty slot (V operation on SEM_EMPTY_SLOTS)
        sem_op(semid, SEM_EMPTY_SLOTS, 1);
        printf("[Consumer %d] Signaled empty slot (EMPTY_SLOTS = %d).\n",
               getpid(), semctl(semid, SEM_EMPTY_SLOTS, GETVAL));
        fflush(stdout);

        // 7. Process the message (using the local copy)
        printf("[Consumer %d] Processing message: Type=%c Size=%d\n",
               getpid(), local_header->type, local_header->size);
        fflush(stdout);

        // 8. Compute and verify hash (using local copy)
        unsigned short computed_hash = compute_hash(local_header, local_data);
        int valid = (computed_hash == local_header->hash);
        printf("[Consumer %d] Hash verification: Computed=0x%X, Received=0x%X, Valid: %s\n",
               getpid(), computed_hash, local_header->hash, valid ? "yes" : "no");
        fflush(stdout);

        if (!valid)
        {
            // Optional: Handle invalid hash cases specifically
            fprintf(stderr, "[Consumer %d] WARNING: Hash mismatch for message from slot %d!\n", getpid(), current_head);
            fflush(stderr);
        }

        // Optional: Add delay if needed
        // struct timespec ts = {0, 50 * 1000000}; // 50 ms
        // nanosleep(&ts, NULL);

    } // End while(running)

    // --- Cleanup ---
    printf("[Consumer %d] Termination signal received or loop ended. Detaching shared memory.\n", getpid());
    fflush(stdout);

    // Detach shared memory segment
    if (queue != (void *)-1)
    {
        if (shmdt(queue) == -1)
        {
            perror("shmdt (consumer)");
        }
        else
        {
            printf("[Consumer %d] Shared memory detached.\n", getpid());
        }
    }

    printf("[Consumer %d] Exiting.\n", getpid());
    fflush(stdout);
    return 0;
}