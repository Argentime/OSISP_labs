#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define MIN_MJD 15020.0
#define MAX_MJD 2460299.0
#define RECORDS_MULTIPLE 256

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
    if (argc != 3)
    {
        fprintf(stderr, "[Main] Usage: %s records filename\n", argv[0]);
        return 1;
    }

    uint64_t records = atoll(argv[1]);
    const char *filename = argv[2];

    if (records % RECORDS_MULTIPLE != 0)
    {
        fprintf(stderr, "[Main] Records must be multiple of %d\n", RECORDS_MULTIPLE);
        return 1;
    }

    int fd = open(filename, O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (fd == -1)
    {
        perror("[Main] open");
        return 1;
    }

    if (write(fd, &records, sizeof(uint64_t)) != sizeof(uint64_t))
    {
        perror("[Main] write header");
        close(fd);
        return 1;
    }

    unsigned int seed = time(NULL);
    for (uint64_t i = 0; i < records; i++)
    {
        struct index_s rec;
        double integer_part = MIN_MJD + (rand_r(&seed) / (double)RAND_MAX) * (MAX_MJD - MIN_MJD);
        double fractional_part = rand_r(&seed) / (double)RAND_MAX;
        rec.time_mark = integer_part + fractional_part;
        rec.recno = i + 1;

        if (write(fd, &rec, sizeof(struct index_s)) != sizeof(struct index_s))
        {
            perror("[Main] write record");
            close(fd);
            return 1;
        }
    }

    printf("[Main] Generated %lu records in %s\n", records, filename);
    fflush(stdout);

    close(fd);
    return 0;
}