#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#define MAX_CHILDREN 100

extern char **environ;

void list_processes(pid_t *children, int child_num, pid_t parent_pid)
{
    printf("[Parent] PID: %d\n", parent_pid);
    for (int i = 0; i < child_num; i++)
    {
        printf("[Child_%02d] PID: %d\n", i, children[i]);
    }
}

void kill_all_children(pid_t *children, int *child_num)
{
    for (int i = 0; i < *child_num; i++)
    {
        kill(children[i], SIGTERM);
        waitpid(children[i], NULL, 0);
    }
    printf("[Parent] All %d children terminated\n", *child_num);
    *child_num = 0;
}

int main(int argc, char *argv[])
{
    pid_t children[MAX_CHILDREN];
    int child_num = 0;
    char *child_path = getenv("CHILD_PATH");
    if (!child_path)
    {
        fprintf(stderr, "[Parent] CHILD_PATH not set\n");
        return EXIT_FAILURE;
    }

    char child_fullpath[256];
    snprintf(child_fullpath, sizeof(child_fullpath), "%s/child", child_path);
    printf("[Parent] Child full path: %s\n", child_fullpath); // Отладка

    printf("[Parent] PID: %d, Enter '+', '-', 'l', 'k', or 'q': ", getpid());
    char input;
    while ((input = getchar()) != EOF)
    {
        if (input == '\n')
            continue;

        if (input == 'q')
        {
            kill_all_children(children, &child_num);
            break;
        }

        if (input == '+')
        {
            if (child_num >= MAX_CHILDREN)
            {
                printf("[Parent] Maximum number of children reached\n");
            }
            else
            {
                pid_t pid = fork();
                if (pid < 0)
                {
                    perror("[Parent] fork");
                }
                else if (pid == 0)
                {
                    char num_str[4];
                    snprintf(num_str, sizeof(num_str), "%02d", child_num);
                    char *child_argv[3] = {"child", num_str, NULL};
                    printf("[Child_%s] Attempting execve: %s\n", num_str, child_fullpath); // Отладка
                    if (execve(child_fullpath, child_argv, environ) == -1)
                    {
                        perror("[Child] execve failed");
                        exit(EXIT_FAILURE);
                    }
                }
                else
                {
                    children[child_num] = pid;
                    child_num++;
                    printf("[Parent] Created child_%02d with PID %d\n", child_num - 1, pid);
                }
            }
        }

        if (input == '-')
        {
            if (child_num > 0)
            {
                child_num--;
                kill(children[child_num], SIGTERM);
                waitpid(children[child_num], NULL, 0);
                printf("[Parent] Terminated child_%02d, %d children remain\n", child_num, child_num);
            }
            else
            {
                printf("[Parent] No children to terminate\n");
            }
        }

        if (input == 'l')
        {
            list_processes(children, child_num, getpid());
        }

        if (input == 'k')
        {
            kill_all_children(children, &child_num);
        }

        printf("[Parent] PID: %d, Enter '+', '-', 'l', 'k', or 'q': ", getpid());
    }

    return 0;
}