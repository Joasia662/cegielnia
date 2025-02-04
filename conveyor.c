#include "conveyor.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <semaphore.h>
#include <sys/stat.h>

#define SEM_FULL "/sem_full"
#define SEM_EMPTY "/sem_empty"
#define SEM_MUTEX "/sem_mutex"

sem_t *sem_full, *sem_empty, *sem_mutex;

shared_memory_t* conveyor_init(size_t max_bricks_count, size_t max_bricks_mass) {
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }

    size_t shm_size = sizeof(shared_memory_t) + max_bricks_count * sizeof(int);
    ftruncate(shm_fd, shm_size);

    shared_memory_t* conveyor = mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
    if (conveyor == MAP_FAILED) {
        perror("mmap failed");
        return NULL;
    }

    conveyor->max_bricks_count = max_bricks_count;
    conveyor->max_bricks_mass = max_bricks_mass;
    conveyor->bricks_count = 0;
    conveyor->bricks_mass = 0;

    // Tworzenie semaforów
    sem_full = sem_open(SEM_FULL, O_CREAT, 0666, 0);
    sem_empty = sem_open(SEM_EMPTY, O_CREAT, 0666, max_bricks_count);
    sem_mutex = sem_open(SEM_MUTEX, O_CREAT, 0666, 1);

    return conveyor;
}

shared_memory_t* conveyor_attach() {
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open failed");
        return NULL;
    }

    size_t shm_size = sizeof(shared_memory_t) + 10 * sizeof(int);
    return mmap(NULL, shm_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
}

void conveyor_insert_brick(shared_memory_t* conveyor, int brick_mass) {
    sem_wait(sem_empty);
    sem_wait(sem_mutex);

    conveyor->bricks[conveyor->bricks_count++] = brick_mass;
    conveyor->bricks_mass += brick_mass;
    printf("[Conveyor] Added brick of weight %d\n", brick_mass);

    sem_post(sem_mutex);
    sem_post(sem_full);
}

int conveyor_remove_brick(shared_memory_t* conveyor) {
    sem_wait(sem_full);
    sem_wait(sem_mutex);

    if (conveyor->bricks_count == 0) {
        sem_post(sem_mutex);
        sem_post(sem_full);
        return -1; // Brak cegieł
    }

    int brick_mass = conveyor->bricks[--conveyor->bricks_count];
    conveyor->bricks_mass -= brick_mass;
    printf("[Conveyor] Removed brick of weight %d\n", brick_mass);

    sem_post(sem_mutex);
    sem_post(sem_empty);

    return brick_mass;
}

void conveyor_destroy() {
    shm_unlink(SHM_NAME);
    sem_unlink(SEM_FULL);
    sem_unlink(SEM_EMPTY);
    sem_unlink(SEM_MUTEX);
}
