#ifndef _WORKER_H_
#define _WORKER_H_

#include <stddef.h>
#include <pthread.h>

#include "conveyor.h"

struct worker_t {
    // worker id
    int id;

    // Weight of the bricks produced by this worker
    size_t produced_brick_weight;

    // Reference to the conveyor structure
    conveyor_t* conveyor;

    // Worker thread
    pthread_t thread_id;
};
typedef struct worker_t worker_t;

// Initialize the worker structure with the ID and weight of bricks, as well as the reference to the conveyor structure
// Does NOT start the thread
worker_t* worker_init(int, size_t, conveyor_t*);

// Start the thread of an initialized worker
// Returns 0 in case of error
int worker_start(worker_t*);


#endif
