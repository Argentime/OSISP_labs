#define _POSIX_C_SOURCE 200809L
#include "common.h"   // Include the common header
#include <sys/wait.h> // For waitpid

#define MAX_PROCESSES 100 // Max producers/consumers main can track

// Global variables for cleanup handler
int shmid = -1;
int semid = -1;
Queue *queue_ptr = (Queue *)-1; // Use a different name to avoid conflict with type 'Queue'
pid_t producer_pids[MAX_PROCESSES];
pid_t consumer_pids[MAX_PROCESSES];
int producer_count = 0;
int consumer_count = 0;

// Cleanup function: Send SIGTERM to children and remove IPC resources
void cleanup()
{
    printf("\n[Main] Cleaning up...\n");

    // Send SIGTERM to all known children
    printf("[Main] Sending SIGTERM to %d producers and %d consumers...\n", producer_count, consumer_count);
    for (int i = 0; i < producer_count; i++)
    {
        if (producer_pids[i] > 0)
        {
            printf("[Main] Sending SIGTERM to producer PID %d\n", producer_pids[i]);
            // Use killpg might be better if children form a process group
            if (kill(producer_pids[i], SIGTERM) == -1 && errno != ESRCH)
            {
                perror("kill producer failed");
            }
        }
    }
    for (int i = 0; i < consumer_count; i++)
    {
        if (consumer_pids[i] > 0)
        {
            printf("[Main] Sending SIGTERM to consumer PID %d\n", consumer_pids[i]);
            if (kill(consumer_pids[i], SIGTERM) == -1 && errno != ESRCH)
            {
                perror("kill consumer failed");
            }
        }
    }

    // Allow some time for children to terminate gracefully
    sleep(1); // Adjust as needed

    // Optional: Wait for children to exit (or send SIGKILL if they don't)
    printf("[Main] Waiting for children to exit...\n");
    int status;
    pid_t exited_pid;
    while ((exited_pid = waitpid(-1, &status, WNOHANG)) > 0)
    {
        printf("[Main] Child PID %d terminated.\n", exited_pid);
        // Remove PID from lists if necessary (more complex tracking)
    }
    // You might add a second loop with SIGKILL for stubborn children

    // Detach shared memory
    if (queue_ptr != (Queue *)-1)
    {
        printf("[Main] Detaching shared memory...\n");
        if (shmdt(queue_ptr) == -1)
        {
            perror("shmdt (main)");
        }
        queue_ptr = (Queue *)-1; // Mark as detached
    }

    // Remove shared memory segment
    if (shmid != -1)
    {
        printf("[Main] Removing shared memory segment (ID: %d)...\n", shmid);
        if (shmctl(shmid, IPC_RMID, NULL) == -1)
        {
            perror("shmctl IPC_RMID (main)");
        }
        shmid = -1; // Mark as removed
    }

    // Remove semaphore set
    if (semid != -1)
    {
        printf("[Main] Removing semaphore set (ID: %d)...\n", semid);
        if (semctl(semid, 0, IPC_RMID) == -1)
        { // sem_num is ignored for IPC_RMID
            perror("semctl IPC_RMID (main)");
        }
        semid = -1; // Mark as removed
    }

    printf("[Main] Cleanup complete.\n");
}

// Signal handler for main process (to trigger cleanup)
void main_signal_handler(int sig)
{
    printf("\n[Main] Signal %d received. Initiating cleanup...\n", sig);
    // Setting a flag is safer if cleanup does complex things,
    // but calling it directly might be okay if it's simple enough
    // and we exit immediately after. Be cautious.
    cleanup();
    exit(EXIT_SUCCESS); // Exit after cleanup
}

int main(int argc, char *argv[])
{
    // --- Setup Signal Handling for main process ---
    struct sigaction sa_main;
    memset(&sa_main, 0, sizeof(sa_main));
    sa_main.sa_handler = main_signal_handler;
    sigaction(SIGTERM, &sa_main, NULL);
    sigaction(SIGINT, &sa_main, NULL); // Handle Ctrl+C

    // Set cleanup function to run on normal exit
    // Note: May not run on abnormal exit or direct exit() calls within main loop
    atexit(cleanup);

    // --- IPC Initialization ---
    key_t key;

    // Generate IPC Key
    key = ftok(IPC_KEY_PATH, IPC_KEY_ID);
    if (key == -1)
    {
        perror("ftok - Ensure '" IPC_KEY_PATH "' exists");
        exit(EXIT_FAILURE);
    }

    // Create or get Shared Memory Segment
    // Use IPC_EXCL with IPC_CREAT first? For robustness.
    shmid = shmget(key, sizeof(Queue), IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("shmget (main creation)");
        exit(EXIT_FAILURE);
    }
    printf("[Main] Shared Memory Segment created/obtained with ID: %d\n", shmid);

    // Attach Shared Memory Segment
    queue_ptr = shmat(shmid, NULL, 0);
    if (queue_ptr == (void *)-1)
    {
        perror("shmat (main)");
        // Cleanup IPC created so far before exiting
        shmctl(shmid, IPC_RMID, NULL);
        shmid = -1;
        exit(EXIT_FAILURE);
    }
    printf("[Main] Shared Memory attached at address: %p\n", (void *)queue_ptr);

    // Create or get Semaphore Set (3 semaphores)
    semid = semget(key, 3, IPC_CREAT | 0666);
    if (semid == -1)
    {
        perror("semget (main creation)");
        shmdt(queue_ptr);
        queue_ptr = (Queue *)-1;
        shmctl(shmid, IPC_RMID, NULL);
        shmid = -1;
        exit(EXIT_FAILURE);
    }
    printf("[Main] Semaphore Set created/obtained with ID: %d\n", semid);

    // --- Initialize Queue and Semaphores ---
    // Check if semaphores were just created (are 0) or already initialized
    struct semid_ds sem_info;
    union semun
    {
        int val;
        struct semid_ds *buf;
        unsigned short *array;
    } arg;
    arg.buf = &sem_info;

    if (semctl(semid, 0, IPC_STAT, arg) == -1)
    {
        perror("semctl IPC_STAT");
        // Cleanup before exit
        cleanup(); // cleanup will handle detach/remove
        exit(EXIT_FAILURE);
    }

    // Initialize only if it seems uninitialized (e.g., check sem_otime or a specific flag)
    // Simple approach: initialize always if main starts first.
    // Robust: Use sem_otime or a dedicated init flag semaphore.
    // Let's assume main starts first and initializes.
    printf("[Main] Initializing Semaphores...\n");
    arg.val = QUEUE_SIZE; // Empty slots
    if (semctl(semid, SEM_EMPTY_SLOTS, SETVAL, arg) == -1)
    {
        perror("semctl SETVAL SEM_EMPTY_SLOTS");
        cleanup();
        exit(EXIT_FAILURE);
    }

    arg.val = 0; // Filled slots
    if (semctl(semid, SEM_FILLED_SLOTS, SETVAL, arg) == -1)
    {
        perror("semctl SETVAL SEM_FILLED_SLOTS");
        cleanup();
        exit(EXIT_FAILURE);
    }

    arg.val = 1; // Mutex (initially unlocked)
    if (semctl(semid, SEM_MUTEX, SETVAL, arg) == -1)
    {
        perror("semctl SETVAL SEM_MUTEX");
        cleanup();
        exit(EXIT_FAILURE);
    }

    // Initialize queue control variables
    // Check if SHM was newly created? Requires more complex logic.
    // Assume main initializes it if it creates it.
    printf("[Main] Initializing Queue head/tail...\n");
    queue_ptr->head = 0;
    queue_ptr->tail = 0;

    // --- Get Paths for Children ---
    char *child_path_env = getenv("CHILD_PATH");
    char child_base_path[256];
    if (!child_path_env)
    {
        // Default to current directory if CHILD_PATH is not set
        printf("[Main] Warning: CHILD_PATH environment variable not set. Assuming children are in the current directory.\n");
        strcpy(child_base_path, ".");
        // fprintf(stderr, "Error: CHILD_PATH environment variable not set.\n");
        // cleanup(); // atexit handles cleanup
        // exit(EXIT_FAILURE);
    }
    else
    {
        strncpy(child_base_path, child_path_env, sizeof(child_base_path) - 1);
        child_base_path[sizeof(child_base_path) - 1] = '\0'; // Ensure null termination
    }

    char prod_path[512], cons_path[512];
    // Use snprintf for safety
    snprintf(prod_path, sizeof(prod_path), "%s/producer", child_base_path);
    snprintf(cons_path, sizeof(cons_path), "%s/consumer", child_base_path);

    // --- Main Control Loop ---
    printf("\n[Main Controller] PID: %d\n", getpid());
    printf("Commands: 'p' (add producer), 'c' (add consumer), 'k' (kill children), 's' (status), 'q' (quit & cleanup)\n");

    char input_buffer[10]; // Buffer for input line
    while (1)
    {
        printf("> ");
        fflush(stdout); // Ensure prompt is shown before reading

        if (fgets(input_buffer, sizeof(input_buffer), stdin) == NULL)
        {
            // EOF or error
            printf("\n[Main] EOF detected or input error. Cleaning up...\n");
            break; // Exit loop, atexit cleanup will run
        }

        // Remove trailing newline if present
        input_buffer[strcspn(input_buffer, "\n")] = 0;

        if (strlen(input_buffer) != 1)
        {
            if (strlen(input_buffer) == 0)
                continue; // Empty line
            printf("[Main] Invalid command. Use single characters: p, c, k, s, q\n");
            continue;
        }

        char command = input_buffer[0];

        if (command == 'q')
        {
            printf("[Main] 'q' command received. Cleaning up and exiting.\n");
            break; // Exit loop, atexit cleanup will run
        }

        switch (command)
        {
        case 'p':
            if (producer_count < MAX_PROCESSES)
            {
                pid_t pid = fork();
                if (pid == -1)
                {
                    perror("fork producer failed");
                }
                else if (pid == 0)
                { // Child process
                    // Child doesn't need parent's signal handlers or atexit handler
                    signal(SIGTERM, SIG_DFL); // Restore default handlers
                    signal(SIGINT, SIG_DFL);
                    execl(prod_path, "producer", (char *)NULL);
                    // If execl returns, an error occurred
                    perror("execl producer failed");
                    // Detach shm before exiting child on error? Optional.
                    exit(EXIT_FAILURE); // Exit child on exec error
                }
                else
                { // Parent process
                    printf("[Main] Created producer with PID %d\n", pid);
                    producer_pids[producer_count++] = pid;
                    // Don't update counts in shared memory anymore
                }
            }
            else
            {
                printf("[Main] Maximum producer count (%d) reached.\n", MAX_PROCESSES);
            }
            break;

        case 'c':
            if (consumer_count < MAX_PROCESSES)
            {
                pid_t pid = fork();
                if (pid == -1)
                {
                    perror("fork consumer failed");
                }
                else if (pid == 0)
                { // Child process
                    signal(SIGTERM, SIG_DFL);
                    signal(SIGINT, SIG_DFL);
                    execl(cons_path, "consumer", (char *)NULL);
                    perror("execl consumer failed");
                    exit(EXIT_FAILURE);
                }
                else
                { // Parent process
                    printf("[Main] Created consumer with PID %d\n", pid);
                    consumer_pids[consumer_count++] = pid;
                    // Don't update counts in shared memory anymore
                }
            }
            else
            {
                printf("[Main] Maximum consumer count (%d) reached.\n", MAX_PROCESSES);
            }
            break;

        case 'k':
            printf("[Main] Killing %d producers and %d consumers...\n", producer_count, consumer_count);
            // Send SIGTERM (more graceful than SIGKILL initially)
            for (int i = 0; i < producer_count; i++)
            {
                if (producer_pids[i] > 0)
                {
                    printf("[Main] Sending SIGTERM to producer PID %d\n", producer_pids[i]);
                    if (kill(producer_pids[i], SIGTERM) == -1 && errno != ESRCH)
                    {
                        perror("kill producer failed");
                    }
                }
            }
            for (int i = 0; i < consumer_count; i++)
            {
                if (consumer_pids[i] > 0)
                {
                    printf("[Main] Sending SIGTERM to consumer PID %d\n", consumer_pids[i]);
                    if (kill(consumer_pids[i], SIGTERM) == -1 && errno != ESRCH)
                    {
                        perror("kill consumer failed");
                    }
                }
            }

            // Wait briefly for them to exit
            sleep(1);

            // Wait for children and clear lists
            printf("[Main] Waiting for killed children to exit...\n");
            int k_status;
            pid_t k_exited_pid;
            int children_remaining = producer_count + consumer_count;
            while (children_remaining > 0 && (k_exited_pid = waitpid(-1, &k_status, WNOHANG)) > 0)
            {
                printf("[Main] Killed child PID %d terminated.\n", k_exited_pid);
                children_remaining--;
                // Optionally, remove from lists here for accurate live counts
            }

            // Clear local counts - assume they were killed or exited
            producer_count = 0;
            consumer_count = 0;
            // Clear PID arrays
            memset(producer_pids, 0, sizeof(producer_pids));
            memset(consumer_pids, 0, sizeof(consumer_pids));

            printf("[Main] Kill command finished.\n");
            break;

        case 's':
            // Calculate occupied slots safely using modulo arithmetic
            // Ensure head/tail are read consistently if needed (mutex? maybe not necessary for rough status)
            int current_head = queue_ptr->head;
            int current_tail = queue_ptr->tail;
            int occupied = (current_tail - current_head + QUEUE_SIZE) % QUEUE_SIZE;
            int free_slots = QUEUE_SIZE - occupied;

            // Get semaphore values
            int empty_val = semctl(semid, SEM_EMPTY_SLOTS, GETVAL);
            int filled_val = semctl(semid, SEM_FILLED_SLOTS, GETVAL);
            int mutex_val = semctl(semid, SEM_MUTEX, GETVAL);

            printf("[Main] --- Status ---\n");
            printf("  Queue:      Size=%d, Head=%d, Tail=%d\n", QUEUE_SIZE, current_head, current_tail);
            printf("  Calculated: Occupied=%d, Free=%d\n", occupied, free_slots);
            printf("  Semaphores: EMPTY_SLOTS=%d, FILLED_SLOTS=%d, MUTEX=%d\n", empty_val, filled_val, mutex_val);
            printf("  Tracked PIDs: Producers=%d, Consumers=%d\n", producer_count, consumer_count);
            printf("  -----------------\n");
            break;

        default:
            printf("[Main] Unknown command: '%c'. Use p, c, k, s, q.\n", command);
            break;
        } // end switch
    } // end while(1)

    printf("[Main] Exiting main loop.\n");
    // cleanup() will be called by atexit if not already called by signal handler

    return 0;
}