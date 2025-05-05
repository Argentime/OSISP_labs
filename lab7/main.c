#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <errno.h>
#include <ctype.h>

// ANSI цветовые коды
#define ANSI_COLOR_RED "\x1b[31m"
#define ANSI_COLOR_GREEN "\x1b[32m"
#define ANSI_COLOR_YELLOW "\x1b[33m"
#define ANSI_COLOR_RESET "\x1b[0m"

#define RECORD_SIZE sizeof(struct record_s)
#define MAX_INPUT 80

struct record_s
{
    char name[80];
    char address[80];
    uint8_t semester;
};

void print_header()
{
    printf(ANSI_COLOR_YELLOW);
    printf("┌────────────────────────────────────────────┐\n");
    printf("│      Student Database Management System    │\n");
    printf("└────────────────────────────────────────────┘\n");
    printf(ANSI_COLOR_RESET);
}

void create_record(int fd)
{
    struct record_s rec = {0};
    char input[MAX_INPUT];

    print_header();
    printf(ANSI_COLOR_GREEN "[Main] Creating new record\n" ANSI_COLOR_RESET);
    printf("[Main] Enter name (max 79 chars, e.g., John Doe): ");
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid name input\n" ANSI_COLOR_RESET);
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) == 0)
    {
        printf(ANSI_COLOR_RED "[Main] Name cannot be empty\n" ANSI_COLOR_RESET);
        return;
    }
    strncpy(rec.name, input, sizeof(rec.name));

    printf("[Main] Enter address (max 79 chars, e.g., 123 Main St): ");
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid address input\n" ANSI_COLOR_RESET);
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) == 0)
    {
        printf(ANSI_COLOR_RED "[Main] Address cannot be empty\n" ANSI_COLOR_RESET);
        return;
    }
    strncpy(rec.address, input, sizeof(rec.address));

    printf("[Main] Enter semester (0-255, e.g., 3): ");
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid semester input\n" ANSI_COLOR_RESET);
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) == 0)
    {
        printf(ANSI_COLOR_RED "[Main] Semester cannot be empty\n" ANSI_COLOR_RESET);
        return;
    }
    int semester = atoi(input);
    if (semester < 0 || semester > 255)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid semester (0-255)\n" ANSI_COLOR_RESET);
        return;
    }
    rec.semester = (uint8_t)semester;

    if (lseek(fd, 0, SEEK_END) == -1)
    {
        perror(ANSI_COLOR_RED "[Main] lseek" ANSI_COLOR_RESET);
        return;
    }
    if (write(fd, &rec, RECORD_SIZE) != RECORD_SIZE)
    {
        perror(ANSI_COLOR_RED "[Main] write" ANSI_COLOR_RESET);
        return;
    }
    printf(ANSI_COLOR_GREEN "[Main] Record created successfully\n" ANSI_COLOR_RESET);
    fflush(stdout);
}

void list_records(int fd)
{
    struct record_s rec;
    int i = 0;

    print_header();
    printf(ANSI_COLOR_GREEN "[Main] Listing all records\n" ANSI_COLOR_RESET);
    if (lseek(fd, 0, SEEK_SET) == -1)
    {
        perror(ANSI_COLOR_RED "[Main] lseek" ANSI_COLOR_RESET);
        return;
    }

    printf("┌─────┬──────────────────────┬──────────────────────┬──────────┐\n");
    printf("│ No. │ Name                 │ Address              │ Semester │\n");
    printf("├─────┼──────────────────────┼──────────────────────┼──────────┤\n");
    while (read(fd, &rec, RECORD_SIZE) == RECORD_SIZE)
    {
        printf("│ %-3d │ %-20.20s │ %-20.20s │ %-8d │\n", i++, rec.name, rec.address, rec.semester);
    }
    printf("└─────┴──────────────────────┴──────────────────────┴──────────┘\n");
    fflush(stdout);
}

void get_record(int fd, int rec_no)
{
    struct record_s rec;

    print_header();
    printf(ANSI_COLOR_GREEN "[Main] Retrieving record %d\n" ANSI_COLOR_RESET, rec_no);
    if (lseek(fd, rec_no * RECORD_SIZE, SEEK_SET) == -1)
    {
        perror(ANSI_COLOR_RED "[Main] lseek" ANSI_COLOR_RESET);
        return;
    }
    if (read(fd, &rec, RECORD_SIZE) == RECORD_SIZE)
    {
        printf("┌───────────────┬──────────────────────┐\n");
        printf("│ Field         │ Value                │\n");
        printf("├───────────────┼──────────────────────┤\n");
        printf("│ Name          │ %-20.20s │\n", rec.name);
        printf("│ Address       │ %-20.20s │\n", rec.address);
        printf("│ Semester      │ %-20d │\n", rec.semester);
        printf("└───────────────┴──────────────────────┘\n");
    }
    else
    {
        printf(ANSI_COLOR_RED "[Main] Record not found\n" ANSI_COLOR_RESET);
    }
    fflush(stdout);
}

void modify_record(struct record_s *rec)
{
    char input[MAX_INPUT];

    print_header();
    printf(ANSI_COLOR_GREEN "[Main] Modifying record\n" ANSI_COLOR_RESET);
    printf("[Main] Enter new name (Enter to keep '%s', max 79 chars): ", rec->name);
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid input\n" ANSI_COLOR_RESET);
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) > 0)
    {
        strncpy(rec->name, input, sizeof(rec->name));
    }

    printf("[Main] Enter new address (Enter to keep '%s', max 79 chars): ", rec->address);
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid input\n" ANSI_COLOR_RESET);
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) > 0)
    {
        strncpy(rec->address, input, sizeof(rec->address));
    }

    printf("[Main] Enter new semester (Enter to keep %d, 0-255): ", rec->semester);
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        printf(ANSI_COLOR_RED "[Main] Invalid input\n" ANSI_COLOR_RESET);
        return;
    }
    input[strcspn(input, "\n")] = '\0';
    if (strlen(input) > 0)
    {
        int semester = atoi(input);
        if (semester >= 0 && semester <= 255)
        {
            rec->semester = (uint8_t)semester;
        }
        else
        {
            printf(ANSI_COLOR_RED "[Main] Invalid semester, keeping old value\n" ANSI_COLOR_RESET);
        }
    }
}

void put_record(int fd, int rec_no, struct record_s *rec)
{
    if (lseek(fd, rec_no * RECORD_SIZE, SEEK_SET) == -1)
    {
        perror(ANSI_COLOR_RED "[Main] lseek" ANSI_COLOR_RESET);
        return;
    }
    if (write(fd, rec, RECORD_SIZE) != RECORD_SIZE)
    {
        perror(ANSI_COLOR_RED "[Main] write" ANSI_COLOR_RESET);
        return;
    }
    printf(ANSI_COLOR_GREEN "[Main] Record saved successfully\n" ANSI_COLOR_RESET);
    fflush(stdout);
}

void lock_record(int fd, int rec_no)
{
    struct flock lock = {
        .l_type = F_WRLCK,
        .l_whence = SEEK_SET,
        .l_start = rec_no * RECORD_SIZE,
        .l_len = RECORD_SIZE};

    printf("[Main] Acquiring lock for record %d", rec_no);
    fflush(stdout);
    for (int i = 0; i < 3; i++)
    {
        printf(".");
        fflush(stdout);
        sleep(1);
    }
    printf("\n");

    if (fcntl(fd, F_OFD_SETLKW, &lock) == -1)
    {
        perror(ANSI_COLOR_RED "[Main] Cannot lock record" ANSI_COLOR_RESET);
        exit(1);
    }
    printf(ANSI_COLOR_GREEN "[Main] Locked record %d\n" ANSI_COLOR_RESET, rec_no);
    fflush(stdout);
}

void unlock_record(int fd, int rec_no)
{
    struct flock lock = {
        .l_type = F_UNLCK,
        .l_whence = SEEK_SET,
        .l_start = rec_no * RECORD_SIZE,
        .l_len = RECORD_SIZE};

    if (fcntl(fd, F_OFD_SETLK, &lock) == -1)
    {
        perror(ANSI_COLOR_RED "[Main] Cannot unlock record" ANSI_COLOR_RESET);
        exit(1);
    }
    printf(ANSI_COLOR_GREEN "[Main] Unlocked record %d\n" ANSI_COLOR_RESET, rec_no);
    fflush(stdout);
}

int check_record_changed(struct record_s *rec1, struct record_s *rec2)
{
    return strcmp(rec1->name, rec2->name) != 0 ||
           strcmp(rec1->address, rec2->address) != 0 ||
           rec1->semester != rec2->semester;
}

int confirm_action(const char *action)
{
    char input[10];
    printf("[Main] Confirm %s? (y/n): ", action);
    if (fgets(input, sizeof(input), stdin) == NULL)
    {
        return 0;
    }
    input[strcspn(input, "\n")] = '\0';
    return tolower(input[0]) == 'y';
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, ANSI_COLOR_RED "[Main] Usage: %s <file>\n" ANSI_COLOR_RESET, argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR | O_CREAT, 0666);
    if (fd == -1)
    {
        perror(ANSI_COLOR_RED "[Main] Cannot open file" ANSI_COLOR_RESET);
        return 1;
    }

    int choice;
    while (1)
    {
        print_header();
        printf("[Main] Menu:\n");
        printf("┌─────┬──────────────────────────────┐\n");
        printf("│ No. │ Action                       │\n");
        printf("├─────┼──────────────────────────────┤\n");
        printf("│ 1   │ List all records             │\n");
        printf("│ 2   │ Get a record                 │\n");
        printf("│ 3   │ Modify a record              │\n");
        printf("│ 4   │ Create a new record          │\n");
        printf("│ 0   │ Exit                         │\n");
        printf("└─────┴──────────────────────────────┘\n");
        printf("[Main] Select choice: ");
        fflush(stdout);

        if (scanf("%d", &choice) != 1)
        {
            printf(ANSI_COLOR_RED "[Main] Invalid input\n" ANSI_COLOR_RESET);
            while (getchar() != '\n')
                ;
            continue;
        }
        while (getchar() != '\n')
            ;

        switch (choice)
        {
        case 1:
            list_records(fd);
            break;
        case 2:
        {
            int rec_no;
            printf("[Main] Enter record number (non-negative): ");
            if (scanf("%d", &rec_no) != 1 || rec_no < 0)
            {
                printf(ANSI_COLOR_RED "[Main] Invalid record number\n" ANSI_COLOR_RESET);
                while (getchar() != '\n')
                    ;
                break;
            }
            while (getchar() != '\n')
                ;
            get_record(fd, rec_no);
            break;
        }
        case 3:
        {
            int rec_no;
            printf("[Main] Enter record number to modify (non-negative): ");
            if (scanf("%d", &rec_no) != 1 || rec_no < 0)
            {
                printf(ANSI_COLOR_RED "[Main] Invalid record number\n" ANSI_COLOR_RESET);
                while (getchar() != '\n')
                    ;
                break;
            }
            while (getchar() != '\n')
                ;

            struct record_s rec, rec_wrk, rec_new;
            if (lseek(fd, rec_no * RECORD_SIZE, SEEK_SET) == -1)
            {
                perror(ANSI_COLOR_RED "[Main] lseek" ANSI_COLOR_RESET);
                break;
            }
            if (read(fd, &rec, RECORD_SIZE) != RECORD_SIZE)
            {
                printf(ANSI_COLOR_RED "[Main] Record not found\n" ANSI_COLOR_RESET);
                break;
            }

            rec_wrk = rec;
            modify_record(&rec_wrk);

            if (check_record_changed(&rec, &rec_wrk))
            {
                if (!confirm_action("save changes"))
                {
                    printf("[Main] Modification cancelled\n");
                    break;
                }

                lock_record(fd, rec_no);

                if (lseek(fd, rec_no * RECORD_SIZE, SEEK_SET) == -1)
                {
                    perror(ANSI_COLOR_RED "[Main] lseek" ANSI_COLOR_RESET);
                    unlock_record(fd, rec_no);
                    break;
                }
                if (read(fd, &rec_new, RECORD_SIZE) != RECORD_SIZE)
                {
                    printf(ANSI_COLOR_RED "[Main] Record not found\n" ANSI_COLOR_RESET);
                    unlock_record(fd, rec_no);
                    break;
                }

                if (check_record_changed(&rec, &rec_new))
                {
                    unlock_record(fd, rec_no);
                    printf(ANSI_COLOR_YELLOW "[Main] Record was modified by another process!\n" ANSI_COLOR_RESET);
                    rec = rec_new;
                    continue;
                }

                put_record(fd, rec_no, &rec_wrk);
                unlock_record(fd, rec_no);
            }
            else
            {
                printf("[Main] No changes made\n");
            }
            break;
        }
        case 4:
            if (confirm_action("create new record"))
            {
                create_record(fd);
            }
            else
            {
                printf("[Main] Creation cancelled\n");
            }
            break;
        case 0:
            printf(ANSI_COLOR_GREEN "[Main] Exiting program\n" ANSI_COLOR_RESET);
            close(fd);
            return 0;
        default:
            printf(ANSI_COLOR_RED "[Main] Invalid command\n" ANSI_COLOR_RESET);
        }
    }
}