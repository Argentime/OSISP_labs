#ifndef COMMON_H
#define COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <signal.h>
#include <errno.h> // For errno checking
#include <time.h>  // For nanosleep (if needed later)

// --- Configuration ---
#define QUEUE_SIZE 10             // Number of slots in the queue
#define MAX_MESSAGE_DATA_SIZE 255 // Maximum size for the *data* part (0-255 for unsigned char size)
// Calculate total size needed per slot in shared memory
#define MAX_MESSAGE_SIZE (sizeof(MessageHeader) + MAX_MESSAGE_DATA_SIZE)
#define IPC_KEY_PATH "/tmp/ipc_key_file" // Ensure this file exists! (touch /tmp/ipc_key_file)
#define IPC_KEY_ID 'Q'

// Semaphore indices (Must be consistent across all files)
#define SEM_EMPTY_SLOTS 0  // Counts empty slots (producer waits/decrements, consumer signals/increments)
#define SEM_FILLED_SLOTS 1 // Counts filled slots (consumer waits/decrements, producer signals/increments)
#define SEM_MUTEX 2        // Binary semaphore for mutual exclusion accessing the queue structure

// --- Message Structure ---
// Header placed at the beginning of each slot in the shared buffer.
typedef struct
{
    char type;           // Message type (optional, example usage)
    unsigned short hash; // Hash computed by the producer over type, size, and data
    unsigned char size;  // Size of the *data* field ONLY (0 to 255)
} MessageHeader;

// --- Shared Memory Queue Structure ---
typedef struct
{
    // Control variables for the ring buffer
    int head; // Index to read from next (consumed by consumer)
    int tail; // Index to write to next (filled by producer)

    // Message storage area within shared memory
    // Each slot holds a complete message (header + data up to MAX_MESSAGE_DATA_SIZE)
    char buffer[QUEUE_SIZE][MAX_MESSAGE_SIZE];

    // Note: Removed producer/consumer counts from shared memory
    // as managing them atomically and reliably is complex and better
    // handled by the main process locally if needed for spawning/killing.

} Queue;

// --- Global variable for signal handling (used by producer/consumer) ---
// Declared as extern so producer/consumer can define it.
extern volatile sig_atomic_t running;

// --- Hash Computation ---
// Computes hash based on type, size, and data fields.
// IMPORTANT: This must be used *identically* by producer and consumer.
static inline unsigned short compute_hash(const MessageHeader *header, const char *data)
{
    unsigned short hash = 0;
    size_t i;

    // Simple XOR hash combining type, size, and data bytes
    hash ^= (unsigned short)(unsigned char)header->type; // Include type
    hash ^= (unsigned short)header->size;                // Include size

    // Include data bytes (up to header->size)
    // Boundary check just in case, though producer should ensure size <= MAX_MESSAGE_DATA_SIZE
    unsigned char data_len = header->size;
    if (data_len > MAX_MESSAGE_DATA_SIZE)
    {
        data_len = MAX_MESSAGE_DATA_SIZE; // Avoid reading out of bounds
    }

    for (i = 0; i < data_len; ++i)
    {
        hash ^= (unsigned short)(unsigned char)data[i];
    }

    return hash;
}

// --- Semaphore Operations ---
// Wrapper for semop with error checking and EINTR handling
static inline int sem_op(int semid, int sem_num, int op)
{
    struct sembuf sb = {(unsigned short)sem_num, (short)op, 0};
    if (semop(semid, &sb, 1) == -1)
    {
        if (errno == EINTR)
        {
            // Interrupted by signal. Return -1 to signal interruption.
            // The caller should check the 'running' flag and decide whether to retry.
            return -1;
        }
        else
        {
            // Other critical semaphore error
            perror("semop failed");
            // Exit here might be too abrupt in some scenarios, but consistent
            // with previous non-EINTR error handling.
            exit(EXIT_FAILURE);
        }
    }
    // Success
    return 0;
}

#endif // COMMON_H