#ifndef _CONVEYOR_H_
#define _CONVEYOR_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

// A brick only contains info about its mass
struct brick_t {
    uint8_t mass;
}

// A structure describing a conveyor belt
struct conveyor_t {
    // Upper limit for the counters below
    size_t max_bricks_count;
    size_t max_bricks_mass;

    // Two counters used to determine whether there is space available in the conveyor
    size_t bricks_count;
    size_t bricks_mass;

    // Because access to counters has to be atomic, synchronization primitives are necessary
    pthread_cond_t space_freed_cond;
    pthread_mutex_t mutex;

    // Storage for the conveyor is done through a pipe, it is crucial to store both ends of the pipe
    int read_fd;
    int write_fd;
};

// Creates a new dynamically allocated conveyor belt structure
conveyor_t* conveyor_init(size_t max_bricks_count, size_t max_bricks_mass);

// Proper cleanup of conveyor belt structure, closing the pipe etc
void conveyor_destroy(conveyor_t*);

void conveyor_insert_brick(conveyor_t*, brick_t);

#endif
