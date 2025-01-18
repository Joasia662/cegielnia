#ifndef _TRUCK_H_
#define _TRUCK_H_

#include <stddef.h>

#include "conveyor.h"

struct truck_t {
    // truck id
    int id;

    // MAx capacity (of mass)
    size_t max_capacity;

    // Current capacity left
    size_t current_capacity;

    // Sleeping time (in seconds)
    unsigned int sleep_time;

    // Reference to the conveyor structure
    conveyor_t* conveyor;

    // Truck thread
    pthread_t thread_id;
};
typedef struct truck_t truck_t;

// Initialize the worker structure with the ID, max weight and sleep time
// as well as the reference to the conveyor structure
// Does NOT start the thread
truck_t* truck_init(int, size_t, unsigned int, conveyor_t*);

// Start the thread of an initialized truck
// Returns 0 in case of error
int truck_start(truck_t*);

#endif
