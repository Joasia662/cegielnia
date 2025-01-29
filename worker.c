#include "worker.h"

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

#include "conveyor.h"

// worker thread main function (defined at the bottom)
void* _worker_main(void*);

// Global stop flag used by worker threads
int _stop_flag = 0;

void worker_stop_flag_set() {
    _stop_flag = 1;
}

int worker_stop_flag_is_set() {
    return _stop_flag;
}

worker_t* worker_init(int id, size_t weight, conveyor_t* c) {
    worker_t* w = malloc(sizeof(worker_t));

    if(!w) {
        return NULL;
    };

    w->id = id;
    w->produced_brick_weight = weight;
    w->conveyor = c;

    return w;
}

// Start the thread of an initialized worker
int worker_start(worker_t* w) {
    if(w->conveyor == NULL) { 
        return 0;
    };

    // Sanity-check
    if(w->id <= 0 || w->produced_brick_weight <= 0) {
        return 0;
    }

    // Try to start thread, and check result
    // We pass address of the _worker_main function as the thread routine
    int result = pthread_create(&(w->thread_id), NULL, &_worker_main, (void*) w);
    if(result != 0) {
        return 0;
    };

    // Return 1 in the calling thread indicating success
    return 1;
}

void* _worker_main(void* arg) {
    worker_t* w = (worker_t*) arg;

    // Store quick-access data in local variables
    conveyor_t* c = w->conveyor;
    int id = w->id;
    size_t weight = w->produced_brick_weight;

    printf("[P%d] Worker started with data: { weight: %zu, conveyor reference: %p }\n", id, weight, (void*) c);

    while(!worker_stop_flag_is_set()) {
        // Create new brick
        brick_t new_brick = { .mass = weight };

        printf("[P%d] trying to insert brick of weight %zu into the conveyor\n", id, weight);
        // Try to insert it
        conveyor_insert_brick(c, new_brick);

        printf("[P%d] EVENT_WORKER_INSERT(%d) Succesfully inserted brick of weight %zu into the conveyor\n", id, id, weight);
    };

    printf("[P%d] Worker saw stop_flag set to 1, finishing work\n", id);

    pthread_exit(NULL);
}
