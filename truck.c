#include "truck.h"

#include "worker.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>

// truck thread main function (defined at the bottom)
void* _truck_main(void*);

truck_t* truck_init(int id, size_t max_capacity, unsigned int sleep_time, conveyor_t* c) {
    truck_t* t = malloc(sizeof(truck_t));

    if(!t) {
        return NULL;
    };

    t->id = id;
    t->max_capacity = max_capacity;
    t->current_capacity = max_capacity; // truck starts empty
    t->sleep_time = sleep_time;
    t->conveyor = c;

    return t;
}

int truck_start(truck_t* t) {
    if(t->conveyor == NULL) { 
        return 0;
    };

    // Sanity-check
    if(t->id <= 0 || t->max_capacity == 0 || t->sleep_time == 0) {
        return 0;
    }

    // Try to start thread, and check result
    // We pass address of the _truck_main function as the thread routine
    int result = pthread_create(&(t->thread_id), NULL, _truck_main, (void*) t);
    if(result != 0) {
        return 0;
    };

    // Return 1 in the calling thread indicating success
    return 1;
}

void* _truck_main(void* arg) {
    truck_t* t = (truck_t*) arg;

    // Store quick-access data in local variables
    // Exception is current_capacity, since it will be changing
    conveyor_t* c = t->conveyor;
    int id = t->id;
    size_t max_capacity = t->max_capacity;
    unsigned int sleep_time = t->sleep_time;

    printf("[C%d] Truck started with data: { max_capacity: %zu, sleep_time: %ds, conveyor reference: %p }\n", id, max_capacity, sleep_time, (void*) c);

    // Reserving-leaving loop
    // Exit once no more bricks
    while(!conveyor_end_of_bricks(c)) {
        // Wait to reserve the conveyor for loading
        conveyor_truck_reserve(c, id);

        printf("[C%d] Truck reserved the conveyor access - loading\n", id);

        // Brick-removing loop
        while(1) {
            printf("[C%d] Truck attempting to remove next brick - capacity: %zu/%zu\n", id, t->current_capacity, max_capacity);

            // Try to remove the next brick
            brick_t new_brick = conveyor_remove_brick(c, t->current_capacity);

            if(new_brick.mass > 0) {
                // Sanity check
                if(new_brick.mass > t->current_capacity) {
                    printf("[C%d] ERROR truck received a brick of mass %zu which exceeds current capacity %zu - THIS SHOULD NEVER HAPPEN\n", id, (size_t) new_brick.mass, t->current_capacity);

                    // leave the conveyor immediately and try to fix the situation during delivery
                    break;
                };

                t->current_capacity -= new_brick.mass;
                printf("[C%d] Truck received a brick of mass %zu - capacity: %zu/%zu\n", id, (size_t) new_brick.mass, t->current_capacity, max_capacity);
            } else {
                // If mass is 0, it means the next brick was not removed as it exceeded the capacity
                // OR theres no more bricks
                // Break out of the inner loop (the brick-removing loop) and the outer loop will check if theres still workers working
                break;
            }
        }

        printf("[C%d] Truck full - leaving\n", id);

        // Once we have broken out of that loop it means
        // that either we can't fit the next brick or an error occured
        conveyor_truck_leave(c, id);

        // Delivering bricks
        sleep(sleep_time);
        t->current_capacity = max_capacity;
    }

    printf("[C%d] Truck finishing work, due to no more bricks\n", id);

    pthread_exit(NULL);
}
