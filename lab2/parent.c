#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>

#define MAX_ENV 128
#define MAX_LINE 256
#define MAX_CHILDREN 100 

int compare_env(const void *a, const void *b)
{
    return strcmp(*(const char **)a, *(const char **)b);
}

char **create_child_env(const char *env_file, char **parent_env)
{
    FILE *fp = fopen(env_file, "r");
    if (!fp)
    {
        perror("fopen env");
        exit(EXIT_FAILURE);
    }

    char **child_env = calloc(MAX_ENV, sizeof(char *));
    if (!child_env)
    {
        perror("calloc");
        exit(EXIT_FAILURE);
    }

    char line[MAX_LINE];
    int i = 0;
    while (fgets(line, MAX_LINE, fp) && i < MAX_ENV - 1)
    {
        line[strcspn(line, "\n")] = 0;
        for (int j = 0; parent_env[j]; j++)
        {
            if (strncmp(parent_env[j], line, strlen(line)) == 0 &&
                parent_env[j][strlen(line)] == '=')
            {
                child_env[i] = strdup(parent_env[j]);
                i++;
                break;
            }
        }
        if(strcmp(line, "LC_COLLATE")==0) {
            child_env[i] = strdup("LC_COLLATE=C");
            i++;
        }
    }
    child_env[i] = NULL;
    fclose(fp);
    return child_env;
}

void free_env(char **env)
{
    for (int i = 0; env[i]; i++)
    {
        free(env[i]);
    }
    free(env);
}

int main(int argc, char *argv[], char *envp[])
{
    if (argc != 2)
    {
        fprintf(stderr, "Usage: %s <env_file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    char *child_path = getenv("CHILD_PATH");
    if (!child_path)
    {
        fprintf(stderr, "CHILD_PATH not set\n");
        return EXIT_FAILURE;
    }
    
    setenv("LC_COLLATE", "C", 1);

    int env_count = 0;
    while (envp[env_count])
        env_count++;
    char **sorted_env = malloc(env_count * sizeof(char *));
    for (int i = 0; i < env_count; i++)
        sorted_env[i] = envp[i];
    qsort(sorted_env, env_count, sizeof(char *), compare_env);
    printf("Parent environment (sorted with LC_COLLATE=C):\n");
    for (int i = 0; i < env_count; i++)
        printf("%s\n", sorted_env[i]);
    free(sorted_env);

    char **child_env = create_child_env(argv[1], envp);
    pid_t children[MAX_CHILDREN];
    int child_num = 0;

    char child_fullpath[256];
    snprintf(child_fullpath, sizeof(child_fullpath), "%s/child", child_path);

    printf("Enter '+', '*', or 'q': ");
    char input;
    while ((input = getchar()) != EOF)
    {
        if (input == '\n')
            continue;

        if (input == 'q')
        {
            for (int i = 0; i < child_num; i++)
            {
                kill(children[i], SIGTERM);    // Отправляем SIGTERM
                waitpid(children[i], NULL, 0);
            }
            free_env(child_env);
            break;
        }

        if (child_num >= MAX_CHILDREN)
        {
            printf("Maximum number of children reached\n");
            continue;
        }

        pid_t pid = fork();
        if (pid < 0)
        {
            perror("fork");
            continue;
        }

        if (pid == 0)
        {
            char num_str[4];
            snprintf(num_str, sizeof(num_str), "%02d", child_num % 100);
            char *child_argv[4];
            child_argv[0] = "child";
            child_argv[1] = num_str;
            child_argv[3] = NULL;

            if (input == '+')
            {
                child_argv[2] = argv[1];
                if (execve(child_fullpath, child_argv, child_env) == -1)
                {
                    perror("execve");
                    exit(EXIT_FAILURE);
                }
            }
            else if (input == '*')
            {
                child_argv[2] = NULL;
                if (execve(child_fullpath, child_argv, child_env) == -1)
                {
                    perror("execve");
                    exit(EXIT_FAILURE);
                }
            }
        }
        else
        {
            children[child_num] = pid; // Сохраняем PID дочернего процесса
            child_num++;
            printf("Created child_%02d with PID %d\n", child_num - 1, pid);
            printf("Enter '+', '*', or 'q': ");
        }
    }

    return 0;
}