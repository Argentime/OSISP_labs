#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>

#define INTERVAL_SEC 0
#define INTERVAL_NSEC 10000000 // 10 мс
#define ITERATIONS 97

typedef struct
{
    int x;
    int y;
} Pair;

typedef struct
{
    int count_00;
    int count_01;
    int count_10;
    int count_11;
} Stats;

static char pid_str[16];

int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <num>\n", argv[0]);
        return EXIT_FAILURE;
    }

    snprintf(pid_str, sizeof(pid_str), "%s", argv[1]);
    printf("[Child_%s] Started, PID: %d, PPID: %d\n", pid_str, getpid(), getppid());
    fflush(stdout);

    signal(SIGTERM, SIG_DFL);

    Pair pair = {0, 0};
    Stats stats = {0, 0, 0, 0};
    int iteration = 0;
    int step = 0;

    struct timespec ts = {INTERVAL_SEC, INTERVAL_NSEC};

    while (1)
    {
        fflush(stdout);

        switch (step % 4)
        {
        case 0:
            pair.x = 0;
            break;
        case 1:
            pair.y = 0;
            break;
        case 2:
            pair.x = 1;
            break;
        case 3:
            pair.y = 1;
            break;
        }
        step++;

        if (nanosleep(&ts, NULL) == -1)
        {
            perror("[Child_%s] nanosleep failed");
            exit(EXIT_FAILURE);
        }

        // Собираем статистику
        if (pair.x == 0 && pair.y == 0)
            stats.count_00++;
        else if (pair.x == 0 && pair.y == 1)
            stats.count_01++;
        else if (pair.x == 1 && pair.y == 0)
            stats.count_10++;
        else if (pair.x == 1 && pair.y == 1)
            stats.count_11++;

        iteration++;
        if (iteration >= ITERATIONS)
        {
            printf("[Child_%s] PPID: %d, PID: %d, Stats: %d %d %d %d\n",
                   pid_str, getppid(), getpid(),
                   stats.count_00, stats.count_01, stats.count_10, stats.count_11);
            fflush(stdout);
            iteration = 0;
            stats.count_00 = stats.count_01 = stats.count_10 = stats.count_11 = 0;
        }
    }

    return 0;
}