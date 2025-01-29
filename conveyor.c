#include "conveyor.h"
#include "worker.h"

#include <errno.h>
#include <pthread.h>
#include <unistd.h> 
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

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
    c->truck_reservation = 0;

    // Attributes structures in both cases can be set to NULL
    // According to manual, these functions never encounter errors
    pthread_mutex_init(&(c->mutex), NULL);
    pthread_cond_init(&(c->space_freed_cond), NULL);
    pthread_cond_init(&(c->new_brick_cond), NULL);
    pthread_cond_init(&(c->truck_left_cond), NULL);

    int fds[2];
    if(pipe(fds) != 0) {
        int errno_tmp = errno;
        pthread_mutex_destroy(&(c->mutex));
        pthread_cond_destroy(&(c->space_freed_cond));
        pthread_cond_destroy(&(c->new_brick_cond));
        pthread_cond_destroy(&(c->truck_left_cond));
        free(c);
        fprintf(stderr, "Error creating pipe - cleanup and return NULL: %s\n", strerror(errno_tmp));
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
    pthread_cond_destroy(&(c->new_brick_cond));
    pthread_cond_destroy(&(c->truck_left_cond));
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
    return c->bricks_count == 0;
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
    ssize_t status_w = write(c->write_fd, (void*) &b, sizeof(brick_t));
    if(status_w > 0 ){
        c->bricks_count++;
        c->bricks_mass += b.mass;
        printf("[CONVEYOR]: EVENT_INSERT(%d) Current count: %zu, current mass: %zu\n", b.mass, c->bricks_count, c->bricks_mass);
    }
    else{
        int errno_tmp = errno;
        fprintf(stderr, "Error while writting to the pipe: %s\n", strerror(errno_tmp));
    }
    
    // Unlock the mutex for other threads to use
    pthread_mutex_unlock(&(c->mutex));

    // Signal that a new brick has arrived on the conveyor
    pthread_cond_signal(&(c->new_brick_cond));
}

// Used by trucks to remove last brick from the conveyor, or return information that it is too big otherwise
// Available capacity should be the available mass capacity left for bricks weight
// Positive return values mean that a brick was removed from the conveyor, and the value is its mass
// Zero means that next brick is too heavy for us to carry
brick_t conveyor_remove_brick(conveyor_t* c, size_t available_capacity) {
    // First ensure exclusive access to the counters by acquiring the mutex
    pthread_mutex_lock(&(c->mutex));

    // If there is no bricks on the conveyor, wait for a signal that one appeared
    while(_conveyor_is_empty(c)) {
        if(!worker_stop_flag_is_set()) { // If there is still workers working, wait for new brick
            pthread_cond_wait(&(c->new_brick_cond), &(c->mutex));
        } else { // Otherwise, return empty brick to signify end of bricks
            pthread_mutex_unlock(&(c->mutex));
            printf("[CONVEYOR]: Current count: %zu, current mass: %zu\n", c->bricks_count, c->bricks_mass);
            brick_t empty_brick = { .mass = 0 };
            return empty_brick;
        }
    }

    
    // If there is no leftover brick, extract one from the pipe
    if(c->leftover_brick.mass == 0) {
        ssize_t status_r = read(c->read_fd, (void*) &(c->leftover_brick), sizeof(brick_t));
        if(status_r <0){
        int errno_tmp = errno;
        fprintf(stderr, "Error while reading from: %s\n", strerror(errno_tmp));
        brick_t invalid_brick = { .mass = 0 };
        return invalid_brick;
        }
    }

    // Now check if we have enough weight available to carry the brick - if not, return 0
    if(c->leftover_brick.mass > available_capacity) {
        pthread_mutex_unlock(&(c->mutex));
        brick_t empty_brick = { .mass = 0 };
        return empty_brick;
    }

    // If we have enough capacity we should remove the brick, change counters, and return its weight
    brick_t brick = c->leftover_brick; // Store the brick to return it
    c->leftover_brick.mass = 0; // Reset leftover brick (remove it from conveyor)
    c->bricks_mass -= brick.mass;
    c-> bricks_count -= 1;

    printf("[CONVEYOR]: EVENT_REMOVE(%d) Current count: %zu, current mass: %zu\n", brick.mass, c->bricks_count, c->bricks_mass);
    
    // do not forget to unlock the mutex and signal that space was freed from the conveyor
    pthread_mutex_unlock(&(c->mutex));
    pthread_cond_signal(&(c->space_freed_cond));

    return brick;
}

int conveyor_end_of_bricks(conveyor_t* c) {
    pthread_mutex_lock(&(c->mutex));

    int result = _conveyor_is_empty(c) && worker_stop_flag_is_set();

    pthread_mutex_unlock(&(c->mutex));
    return result;
}

// Function used by trucks to let everyone know they are now using the conveyor
// (blocks until current truck leaves, if any)
void conveyor_truck_reserve(conveyor_t* c, int id) {
    // First ensure exclusive access to the conveyor
    pthread_mutex_lock(&(c->mutex));

    // While there is some other reservation, wait patiently for
    // the signal that current truck has left
    while(c->truck_reservation != 0) {
        pthread_cond_wait(&(c->truck_left_cond), &(c->mutex));
    };

    // Once truck_reservation is empty, we can claim it
    c->truck_reservation = id;

    // Unlock the mutex afterwards
    pthread_mutex_unlock(&(c->mutex));
}

// Function used by trucks to let everyone know they are leaving the conveyor
void conveyor_truck_leave(conveyor_t* c, int id) {
    // First ensure exclusive access to the conveyor
    pthread_mutex_lock(&(c->mutex));

    // sanity check - only free the reservation, if we had the reservation
    // in the first place
    if(c->truck_reservation == id)
        c->truck_reservation = 0;

    // Unlock the mutex afterwards
    pthread_mutex_unlock(&(c->mutex));

    // Let other trucks know that we left the conveyor
    pthread_cond_signal(&(c->truck_left_cond));
}
