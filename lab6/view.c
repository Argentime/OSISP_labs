#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

struct index_s
{
    double time_mark;
    uint64_t recno;
};

struct index_hdr_s
{
    uint64_t records;
    struct index_s idx[];
};

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "[Main] Usage: %s filename\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDONLY);
    if (fd == -1)
    {
        perror("[Main] open");
        return 1;
    }

    uint64_t records;
    if (read(fd, &records, sizeof(uint64_t)) != sizeof(uint64_t))
    {
        perror("[Main] read header");
        close(fd);
        return 1;
    }

    printf("[Main] Records: %lu\n", records);
    fflush(stdout);

    struct index_s rec;
    for (uint64_t i = 0; i < records; i++)
    {
        if (read(fd, &rec, sizeof(struct index_s)) != sizeof(struct index_s))
        {
            perror("[Main] read record");
            close(fd);
            return 1;
        }
        printf("[Main] time_mark: %.6f, recno: %lu\n", rec.time_mark, rec.recno);
        fflush(stdout);
    }

    close(fd);
    return 0;
}