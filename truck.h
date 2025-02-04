#ifndef TRUCK_H
#define TRUCK_H

#include <stdlib.h>
#include "conveyor.h"

typedef struct {
    int id;
    size_t max_capacity;
    size_t current_capacity;
    unsigned int sleep_time;
    shared_memory_t* conveyor;
} truck_t;

truck_t* truck_init(int, size_t, unsigned int, shared_memory_t*);
int truck_start(truck_t*);

#endif
