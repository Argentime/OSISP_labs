#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#define MAX_LINE 256

volatile sig_atomic_t running = 1;

void signal_handler()
{
    running = 0;
}

int main(int argc, char *argv[], char *envp[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Usage: %s <num> [env_file]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // Устанавливаем обработчик сигнала SIGTERM
    signal(SIGTERM, signal_handler);

    printf("Child process: %s_%s, PID: %d, PPID: %d\n", argv[0], argv[1], getpid(), getppid());

    if (argc == 3)
    { // Режим "+"
        FILE *fp = fopen(argv[2], "r");
        if (!fp)
        {
            perror("fopen env");
            return EXIT_FAILURE;
        }

        char line[MAX_LINE];
        printf("Environment from env file:\n");
        while (fgets(line, MAX_LINE, fp))
        {
            line[strcspn(line, "\n")] = 0;
            char *value = getenv(line);
            if (value)
                printf("%s=%s\n", line, value);
        }
        fclose(fp);
    }
    else
    { // Режим "*"
        printf("Environment from envp:\n");
        for (int i = 0; envp[i]; i++)
        {
            printf("%s\n", envp[i]);
        }
    }

    while (running)
    {
        sleep(2); // Задержка для имитации работы
    }
    
    printf("Child %s_%s (PID %d) terminating\n", argv[0], argv[1], getpid());
    return 0;
}