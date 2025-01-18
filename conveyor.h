#ifndef _CONVEYOR_H_
#define _CONVEYOR_H_

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

// A brick only contains info about its mass
struct brick_t {
    uint8_t mass;
};
typedef struct brick_t brick_t;

// A structure describing a conveyor belt
struct conveyor_t {
    // Upper limit for the counters below
    size_t max_bricks_count;
    size_t max_bricks_mass;

    // Two counters used to determine whether there is space available in the conveyor
    // They include the leftover_brick field
    size_t bricks_count;
    size_t bricks_mass;

    // If leftover_brick has weight larger than 0 it means it was left by previous truck
    // Next truck should pick it up if it can
    brick_t leftover_brick;

    // ID of a truck that is currently being loaded from the conveyor
    // Equal to 0 if conveyor is not loading any truck at the moment
    int truck_reservation;

    // Because access to counters has to be atomic, synchronization primitives are necessary
    pthread_cond_t space_freed_cond; // Conditional signaled by trucks when they remove a brick and free some space in this way
    pthread_cond_t new_brick_cond; // Conditional signaled by workers when they insert a new brick into conveyor
    pthread_cond_t truck_left_cond; // Conditional signaled by trucks to other trucks when they leave conveyor belt after being full
    pthread_mutex_t mutex; // Access to conveyor and its counters

    // Storage for the conveyor is done through a pipe, it is crucial to store both ends of the pipe
    int read_fd;
    int write_fd;
};
typedef struct conveyor_t conveyor_t;

// Creates a new dynamically allocated conveyor belt structure
conveyor_t* conveyor_init(size_t max_bricks_count, size_t max_bricks_mass);

// Proper cleanup of conveyor belt structure, closing the pipe etc
void conveyor_destroy(conveyor_t*);

// Used by workers to insert bricks
void conveyor_insert_brick(conveyor_t*, brick_t);

// Used by trucks to load a brick from the conveyor
// Returns size of brick if succesful, or 0 if next brick exceeds capacity
brick_t conveyor_remove_brick(conveyor_t*, size_t);

// Function used by trucks to let everyone know they are now using the conveyor
// (blocks until current truck leaves, if any)
// second argument is truck id
void conveyor_truck_reserve(conveyor_t*, int);

// Function used by trucks to let everyone know they are leaving the conveyor
// second argument is truck id - used for checking if we have the truck reserved
void conveyor_truck_leave(conveyor_t*, int);

#endif
