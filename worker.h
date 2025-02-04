#ifndef WORKER_H
#define WORKER_H

#include <stdlib.h>
#include "conveyor.h"

typedef struct {
    int id;
    size_t produced_brick_weight;
    shared_memory_t* conveyor;
} worker_t;

worker_t* worker_init(int, size_t, shared_memory_t*);
int worker_start(worker_t*);

#endif
