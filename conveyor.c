#include "conveyor.h"

#include <pthread.h>
#include <unistd.h>
#include <stdlib.h>

// Creates a new dynamically allocated conveyor belt structure
conveyor_t* conveyor_init(size_t max_bricks_count, size_t max_bricks_mass) {
    conveyor_t* c = malloc(sizeof(conveyor_t));

    c->max_bricks_count = max_bricks_count;
    c->max_bricks_mass = max_bricks_mass;
    c->bricks_count = 0;
    c->bricks_mass = 0;

    // Attributes structures in both cases can be set to NULL
    // According to manual, these functions never encounter errors
    pthread_mutex_init(&(c->mutex), NULL);
    pthread_cond_init(&(c->space_freed_cond), NULL);

    int fds[2];
    if(pipe(fds) != 0) {
        // Error creating pipe - cleanup and return NULL
        pthread_mutex_destroy(&(c->mutex), NULL);
        pthread_cond_destroy(&(c->space_freed_cond), NULL);
        free(c);

        return NULL;
    }

    c->write_fd = fds[1];
    c->read_fd = fds[0];
}

// Proper cleanup of conveyor belt structure, closing the pipe and destroying the synchronization primitives
void conveyor_destroy(conveyor_t* c) {
    pthread_mutex_destroy(&(c->mutex));
    pthread_cond_destroy(&(c->space_freed_cond));
    close(c->read_fd);
    close(c->write_fd)
    free(c);
}

// Helper function to check if:
//  1) There is space for at least 1 more brick AND
//  2) The bricks mass won't exceed the maximum
int _conveyor_has_space_for_brick(conveyor_t* c, brick_t b) {
    return (c->bricks_count < c->max_bricks_count) && (c->bricks_mass + b.mass <= c->max_bricks_mass);
}

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

    // Finally, unlock the mutex for other threads to use
    pthread_mutex_unlock(&(c->mutex));
}
