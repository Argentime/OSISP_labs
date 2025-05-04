#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <pthread.h>

#define MAX_THREADS 8192
#define MIN_BLOCKS_PER_THREAD 4
#define RECORD_SIZE sizeof(struct index_s)

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

struct sort_context
{
    struct index_s *buffer;
    size_t memsize;
    size_t record_size;
    size_t records_per_part;
    int blocks;
    int threads;
    pthread_barrier_t barrier;
    pthread_mutex_t mutex;
    int *block_map;
    struct index_s *tmp_buf;
};

struct thread_data
{
    int thread_id;
    struct sort_context *ctx;
};

int compare_index(const void *a, const void *b)
{
    const struct index_s *ia = (const struct index_s *)a;
    const struct index_s *ib = (const struct index_s *)b;
    if (ia->time_mark < ib->time_mark)
        return -1;
    if (ia->time_mark > ib->time_mark)
        return 1;
    return 0;
}

void merge(struct index_s *dest, struct index_s *a, size_t a_len, struct index_s *b, size_t b_len)
{
    size_t i = 0, j = 0, k = 0;
    while (i < a_len && j < b_len)
    {
        if (compare_index(&a[i], &b[j]) <= 0)
        {
            dest[k++] = a[i++];
        }
        else
        {
            dest[k++] = b[j++];
        }
    }
    while (i < a_len)
        dest[k++] = a[i++];
    while (j < b_len)
        dest[k++] = b[j++];
}

void *thread_func(void *arg)
{
    struct thread_data *data = (struct thread_data *)arg;
    int tid = data->thread_id;
    struct sort_context *ctx = data->ctx;
    struct index_s *buffer = ctx->buffer;
    size_t block_records = ctx->records_per_part / ctx->blocks;

    // Сортировка начального блока
    if (tid < ctx->blocks)
    {
        printf("[Thread %d] Sorting block: %d\n", tid, tid);
        fflush(stdout);
        qsort(&buffer[tid * block_records], block_records, ctx->record_size, compare_index);
        pthread_mutex_lock(&ctx->mutex);
        ctx->block_map[tid] = 2; // Отсортирован
        pthread_mutex_unlock(&ctx->mutex);
        printf("[Thread %d] Sorted block: %d\n", tid, tid);
        fflush(stdout);
    }

    // Сортировка остальных блоков
    while (1)
    {
        pthread_mutex_lock(&ctx->mutex);
        int free_block = -1;
        for (int i = 0; i < ctx->blocks; i++)
        {
            if (ctx->block_map[i] == 0)
            {
                free_block = i;
                ctx->block_map[i] = 1; // Занят
                break;
            }
        }
        pthread_mutex_unlock(&ctx->mutex);

        if (free_block == -1)
        {
            printf("[Thread %d] Waiting at sort barrier\n", tid);
            fflush(stdout);
            pthread_barrier_wait(&ctx->barrier);
            break;
        }

        printf("[Thread %d] Sorting block: %d\n", tid, free_block);
        fflush(stdout);
        qsort(&buffer[free_block * block_records], block_records, ctx->record_size, compare_index);
        pthread_mutex_lock(&ctx->mutex);
        ctx->block_map[free_block] = 2; // Отсортирован
        pthread_mutex_unlock(&ctx->mutex);
        printf("[Thread %d] Sorted block: %d\n", tid, free_block);
        fflush(stdout);
    }

    // Фаза слияния
    size_t current_block_records = block_records;
    int current_blocks = ctx->blocks;
    while (current_blocks > 1)
    {
        int pairs = current_blocks / 2;
        for (int i = tid; i < pairs; i += ctx->threads)
        {
            size_t start = 2 * i * current_block_records;
            size_t mid = start + current_block_records;
            printf("[Thread %d] Merging blocks: %zu-%zu\n", tid, start / block_records, (mid / block_records) - 1);
            fflush(stdout);
            merge(&ctx->tmp_buf[start], &buffer[start], current_block_records,
                  &buffer[mid], current_block_records);
            memcpy(&buffer[start], &ctx->tmp_buf[start], 2 * current_block_records * ctx->record_size);
            printf("[Thread %d] Merged blocks: %zu-%zu\n", tid, start / block_records, (mid / block_records) - 1);
            fflush(stdout);
        }
        printf("[Thread %d] Waiting at merge barrier\n", tid);
        fflush(stdout);
        pthread_barrier_wait(&ctx->barrier);
        current_block_records *= 2;
        current_blocks = (current_blocks + 1) / 2;
    }

    return NULL;
}

int merge_parts(int fd, int num_parts, size_t records_per_part, size_t record_size, const char *filename)
{
    int tmp_fd = open("tmp.sorted", O_CREAT | O_WRONLY | O_TRUNC, 0666);
    if (tmp_fd == -1)
    {
        perror("[Main] open tmp");
        return 1;
    }

    uint64_t total_records = num_parts * records_per_part;
    if (write(tmp_fd, &total_records, sizeof(uint64_t)) != sizeof(uint64_t))
    {
        perror("[Main] write header");
        close(tmp_fd);
        return 1;
    }

    struct merge_state
    {
        struct index_s current;
        size_t pos;
        size_t end;
        int active;
    };

    struct merge_state *states = malloc(sizeof(struct merge_state) * num_parts);
    if (!states)
    {
        perror("[Main] malloc states");
        close(tmp_fd);
        return 1;
    }

    for (int i = 0; i < num_parts; i++)
    {
        states[i].pos = sizeof(uint64_t) + i * records_per_part * record_size;
        states[i].end = states[i].pos + records_per_part * record_size;
        if (lseek(fd, states[i].pos, SEEK_SET) == -1)
        {
            perror("[Main] lseek");
            free(states);
            close(tmp_fd);
            return 1;
        }
        if (read(fd, &states[i].current, record_size) != (ssize_t)record_size)
        {
            perror("[Main] read initial record");
            free(states);
            close(tmp_fd);
            return 1;
        }
        states[i].pos += record_size;
        states[i].active = 1;
    }

    while (1)
    {
        int min_idx = -1;
        for (int i = 0; i < num_parts; i++)
        {
            if (states[i].active)
            {
                if (min_idx == -1 || compare_index(&states[i].current, &states[min_idx].current) < 0)
                {
                    min_idx = i;
                }
            }
        }
        if (min_idx == -1)
            break;

        if (write(tmp_fd, &states[min_idx].current, record_size) != (ssize_t)record_size)
        {
            perror("[Main] write merged record");
            free(states);
            close(tmp_fd);
            return 1;
        }

        if (states[min_idx].pos < states[min_idx].end)
        {
            if (lseek(fd, states[min_idx].pos, SEEK_SET) == -1)
            {
                perror("[Main] lseek");
                free(states);
                close(tmp_fd);
                return 1;
            }
            if (read(fd, &states[min_idx].current, record_size) != (ssize_t)record_size)
            {
                perror("[Main] read next record");
                free(states);
                close(tmp_fd);
                return 1;
            }
            states[min_idx].pos += record_size;
        }
        else
        {
            states[min_idx].active = 0;
        }
    }

    free(states);
    close(tmp_fd);

    if (rename("tmp.sorted", filename) == -1)
    {
        perror("[Main] rename");
        return 1;
    }

    printf("[Main] Merged %d parts into %s\n", num_parts, filename);
    fflush(stdout);
    return 0;
}

int validate_args(size_t memsize, int blocks, int threads, uint64_t records)
{
    int page_size = sysconf(_SC_PAGESIZE);
    if (memsize % page_size != 0)
    {
        fprintf(stderr, "[Main] memsize must be multiple of page size (%d)\n", page_size);
        return 1;
    }
    if ((blocks & (blocks - 1)) != 0)
    {
        fprintf(stderr, "[Main] blocks must be power of two\n");
        return 1;
    }
    if (blocks < MIN_BLOCKS_PER_THREAD * threads)
    {
        fprintf(stderr, "[Main] blocks must be at least %d*threads\n", MIN_BLOCKS_PER_THREAD);
        return 1;
    }
    if (threads < 1 || threads > MAX_THREADS)
    {
        fprintf(stderr, "[Main] threads must be between 1 and %d\n", MAX_THREADS);
        return 1;
    }
    if (memsize % RECORD_SIZE != 0)
    {
        fprintf(stderr, "[Main] memsize must be multiple of record size (%zu)\n", RECORD_SIZE);
        return 1;
    }
    size_t records_per_part = memsize / RECORD_SIZE;
    if (records % records_per_part != 0)
    {
        fprintf(stderr, "[Main] file records must be multiple of records per part\n");
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 5)
    {
        fprintf(stderr, "[Main] Usage: %s memsize blocks threads filename\n", argv[0]);
        return 1;
    }

    size_t memsize = atoll(argv[1]);
    int blocks = atoi(argv[2]);
    int threads = atoi(argv[3]);
    const char *filename = argv[4];

    int fd = open(filename, O_RDWR);
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

    if (validate_args(memsize, blocks, threads, records))
    {
        close(fd);
        return 1;
    }

    size_t records_per_part = memsize / RECORD_SIZE;
    int num_parts = records / records_per_part;

    for (int part = 0; part < num_parts; part++)
    {
        size_t data_offset = sizeof(uint64_t) + part * memsize;
        size_t aligned_offset = (data_offset / sysconf(_SC_PAGESIZE)) * sysconf(_SC_PAGESIZE);
        size_t skip_bytes = data_offset - aligned_offset;
        size_t map_size = memsize + skip_bytes;

        void *mmap_buf = mmap(NULL, map_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, aligned_offset);
        if (mmap_buf == MAP_FAILED)
        {
            perror("[Main] mmap");
            close(fd);
            return 1;
        }

        struct index_s *buffer = (struct index_s *)((char *)mmap_buf + skip_bytes);

        struct sort_context ctx = {
            .buffer = buffer,
            .memsize = memsize,
            .record_size = RECORD_SIZE,
            .records_per_part = records_per_part,
            .blocks = blocks,
            .threads = threads};

        if (pthread_barrier_init(&ctx.barrier, NULL, threads) != 0)
        {
            perror("[Main] pthread_barrier_init");
            munmap(mmap_buf, map_size);
            close(fd);
            return 1;
        }
        if (pthread_mutex_init(&ctx.mutex, NULL) != 0)
        {
            perror("[Main] pthread_mutex_init");
            pthread_barrier_destroy(&ctx.barrier);
            munmap(mmap_buf, map_size);
            close(fd);
            return 1;
        }

        ctx.block_map = calloc(blocks, sizeof(int));
        if (!ctx.block_map)
        {
            perror("[Main] calloc block_map");
            pthread_mutex_destroy(&ctx.mutex);
            pthread_barrier_destroy(&ctx.barrier);
            munmap(mmap_buf, map_size);
            close(fd);
            return 1;
        }
        for (int i = 0; i < blocks; i++)
        {
            ctx.block_map[i] = (i < threads) ? 1 : 0;
        }

        ctx.tmp_buf = malloc(memsize);
        if (!ctx.tmp_buf)
        {
            perror("[Main] malloc tmp_buf");
            free(ctx.block_map);
            pthread_mutex_destroy(&ctx.mutex);
            pthread_barrier_destroy(&ctx.barrier);
            munmap(mmap_buf, map_size);
            close(fd);
            return 1;
        }

        pthread_t *thread_ids = NULL;
        struct thread_data *datas = NULL;
        if (threads > 1)
        {
            thread_ids = malloc(sizeof(pthread_t) * (threads - 1));
            if (!thread_ids)
            {
                perror("[Main] malloc thread_ids");
                goto cleanup;
            }
            datas = malloc(sizeof(struct thread_data) * threads);
            if (!datas)
            {
                perror("[Main] malloc datas");
                free(thread_ids);
                goto cleanup;
            }

            for (int t = 1; t < threads; t++)
            {
                datas[t].thread_id = t;
                datas[t].ctx = &ctx;
                if (pthread_create(&thread_ids[t - 1], NULL, thread_func, &datas[t]) != 0)
                {
                    perror("[Main] pthread_create");
                    for (int u = 0; u < t - 1; u++)
                    {
                        pthread_cancel(thread_ids[u]);
                    }
                    free(datas);
                    free(thread_ids);
                    goto cleanup;
                }
            }
        }

        datas = malloc(sizeof(struct thread_data) * threads);
        if (!datas)
        {
            perror("[Main] malloc datas");
            if (thread_ids)
                free(thread_ids);
            goto cleanup;
        }
        datas[0].thread_id = 0;
        datas[0].ctx = &ctx;
        thread_func(&datas[0]);

        if (threads > 1)
        {
            for (int t = 1; t < threads; t++)
            {
                if (pthread_join(thread_ids[t - 1], NULL) != 0)
                {
                    perror("[Main] pthread_join");
                }
            }
        }

        printf("[Main] Processed part %d of %d\n", part + 1, num_parts);
        fflush(stdout);

    cleanup:
        if (ctx.tmp_buf)
            free(ctx.tmp_buf);
        if (ctx.block_map)
            free(ctx.block_map);
        pthread_mutex_destroy(&ctx.mutex);
        pthread_barrier_destroy(&ctx.barrier);
        munmap(mmap_buf, map_size);
        if (thread_ids)
            free(thread_ids);
        if (datas)
            free(datas);
    }

    if (merge_parts(fd, num_parts, records_per_part, RECORD_SIZE, filename) != 0)
    {
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}