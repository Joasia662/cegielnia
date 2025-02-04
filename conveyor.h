#ifndef CONVEYOR_H
#define CONVEYOR_H

#include <semaphore.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#define SHM_NAME "/conveyor_shm"

typedef struct {
    size_t max_bricks_count;
    size_t max_bricks_mass;
    size_t bricks_count;
    size_t bricks_mass;
    int bricks[];  // Elastyczna tablica
} shared_memory_t;

shared_memory_t* conveyor_init(size_t max_bricks_count, size_t max_bricks_mass);
shared_memory_t* conveyor_attach();
void conveyor_insert_brick(shared_memory_t* conveyor, int brick_mass);
int conveyor_remove_brick(shared_memory_t* conveyor);
void conveyor_destroy();

#endif
