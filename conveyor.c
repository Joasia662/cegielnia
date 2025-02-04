#include "conveyor.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <semaphore.h>
#include <string.h>

#define SHM_NAME "/conveyor_shm"
#define SEM_FULL "/sem_full"
#define SEM_EMPTY "/sem_empty"
#define SEM_MUTEX "/sem_mutex"

typedef struct
{
    int max_bricks_count;
    int bricks_count;
    int bricks[]; // Elastyczna tablica (variable-length array - VLA)
} shared_memory_t;

conveyor_t *conveyor_init(size_t max_bricks_count, size_t max_bricks_mass)
{
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    size_t shm_size = sizeof(shared_memory_t) + max_bricks_count * sizeof(int);
    ftruncate(shm_fd, shm_size);

    shared_memory_t *conveyor = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    conveyor->max_bricks_count = max_bricks_count;
    conveyor->bricks_count = 0;

    sem_t *sem_full = sem_open(SEM_FULL, O_CREAT, 0666, 0);
    sem_t *sem_empty = sem_open(SEM_EMPTY, O_CREAT, 0666, max_bricks_count);
    sem_t *sem_mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);

    conveyor->sem_full = sem_full;
    conveyor->sem_empty = sem_empty;
    conveyor->sem_mutex = sem_mutex;

    return conveyor;
}

conveyor_t *conveyor_attach()
{
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    return mmap(NULL, sizeof(conveyor_t), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
}

void conveyor_insert_brick(conveyor_t *conveyor, brick_t brick)
{
    sem_wait(conveyor->sem_empty);
    sem_wait(conveyor->sem_mutex);

    conveyor->bricks[conveyor->bricks_count++] = brick.mass;
    printf("[Conveyor] Brick added, count: %d\n", conveyor->bricks_count);

    sem_post(conveyor->sem_mutex);
    sem_post(conveyor->sem_full);
}

brick_t conveyor_remove_brick(conveyor_t *conveyor, size_t available_capacity)
{
    sem_wait(conveyor->sem_full);
    sem_wait(conveyor->sem_mutex);

    brick_t brick = {.mass = conveyor->bricks[--conveyor->bricks_count]};
    printf("[Conveyor] Brick removed, count: %d\n", conveyor->bricks_count);

    sem_post(conveyor->sem_mutex);
    sem_post(conveyor->sem_empty);

    return brick;
}

void conveyor_destroy(conveyor_t *conveyor)
{
    sem_close(conveyor->sem_full);
    sem_close(conveyor->sem_empty);
    sem_close(conveyor->sem_mutex);

    sem_unlink(SEM_FULL);
    sem_unlink(SEM_EMPTY);
    sem_unlink(SEM_MUTEX);

    shm_unlink(SHM_NAME);
}
