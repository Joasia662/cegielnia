#include "conveyor.h"

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

#include <stdio.h>

// Creates a new dynamically allocated conveyor belt structure
conveyor_t* conveyor_init(size_t max_bricks_count, size_t max_bricks_mass) {
    conveyor_t* c = malloc(sizeof(conveyor_t));
    if(!c) {
        return NULL;
    }

    c->max_bricks_count = max_bricks_count;
    c->max_bricks_mass = max_bricks_mass;
    c->bricks_count = 0;
    c->bricks_mass = 0;
    c->leftover_brick.mass = 0;

    // Attributes structures in both cases can be set to NULL
    // According to manual, these functions never encounter errors
    pthread_mutex_init(&(c->mutex), NULL);
    pthread_cond_init(&(c->space_freed_cond), NULL);

    int fds[2];
    if(pipe(fds) != 0) {
        // Error creating pipe - cleanup and return NULL
        pthread_mutex_destroy(&(c->mutex));
        pthread_cond_destroy(&(c->space_freed_cond));
        free(c);

        return NULL;
    }

    c->write_fd = fds[1];
    c->read_fd = fds[0];

    return c;
}

// Proper cleanup of conveyor belt structure, closing the pipe and destroying the synchronization primitives
void conveyor_destroy(conveyor_t* c) {
    pthread_mutex_destroy(&(c->mutex));
    pthread_cond_destroy(&(c->space_freed_cond));
    close(c->read_fd);
    close(c->write_fd);
    free(c);
}

// Helper function to check if:
//  1) There is space for at least 1 more brick AND
//  2) The bricks mass won't exceed the maximum
int _conveyor_has_space_for_brick(conveyor_t* c, brick_t b) {
    return (c->bricks_count < c->max_bricks_count) && (c->bricks_mass + b.mass <= c->max_bricks_mass);
}

// Helper function to check if conveyor is empty
int _conveyor_is_empty(conveyor_t* c) {
    return c->bricks_count > 0;
}

// Used by workers to insert new bricks onto the conveyor
void conveyor_insert_brick(conveyor_t* c, brick_t b) {
    // First ensure exclusive access to the counters by acquiring the mutex
    pthread_mutex_lock(&(c->mutex));

    // If its not possible to fit the brick into the conveyor, wait for a signal
    // from a truck that space was freed
    while(!_conveyor_has_space_for_brick(c, b)) {
        pthread_cond_wait(&(c->space_freed_cond), &(c->mutex));
    }

    // After exiting the loop we have acquired the mutex and are sure there is enough space in the conveyor
    // Write the 1-byte brick to the pipe, and update the counters
    write(c->write_fd, (void*) &b, sizeof(brick_t));
    c->bricks_count++;
    c->bricks_mass += b.mass;

    printf("DEBUG CONVEYOR: current count: %zu, current mass: %zu\n", c->bricks_count, c->bricks_mass);

    // Unlock the mutex for other threads to use
    pthread_mutex_unlock(&(c->mutex));

    // Signal that a new brick has arrived on the conveyor
    pthread_cond_signal(&(c->new_brick_cond));
}

// Used by trucks to remove last brick from the conveyor, or return information that it is too big otherwise
// Available capacity should be the available mass capacity left for bricks weight
// Positive return values mean that a brick was removed from the conveyor, and the value is its mass
// Zero means that next brick is too heavy for us to carry
size_t conveyor_remove_brick(conveyor_t* c, size_t available_capacity) {
    // First ensure exclusive access to the counters by acquiring the mutex
    pthread_mutex_lock(&(c->mutex));

    // If its not possible to fit the brick into the conveyor, wait for a signal
    // from a truck that space was freed
    while(_conveyor_is_empty(c)) {
        pthread_cond_wait(&(c->new_brick_cond), &(c->mutex));
    }

    // If there is no leftover brick, extract one from the pipe
    if(c->leftover_brick.mass == 0) {
        read(c->read_fd, (void*) &(c->leftover_brick), sizeof(brick_t));
    }

    // Now check if we have enough weight available to carry the brick - if not, return 0
    if(c->leftover_brick.mass > available_capacity) {
        pthread_mutex_unlock(&(c->mutex));
        return 0;
    }

    // If we have enough capacity we should remove the brick, change counters, and return its weight
    size_t mass = c->leftover_brick.mass; // Store mass to return it
    c->leftover_brick.mass = 0; // Reset leftover brick (remove it from conveyor)
    c->bricks_mass -= mass;
    c-> bricks_count -= 1;
    
    // do not forget to unlock the mutex and signal that space was freed from the conveyor
    pthread_mutex_unlock(&(c->mutex));
    pthread_cond_signal(&(c->space_freed_cond));

    return mass;
}
