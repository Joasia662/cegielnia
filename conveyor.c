#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdint.h>
#include <semaphore.h>
#include <time.h>

#include "common.h"
#include "messages.h"

sem_t* zone_sem;
shared_loading_zone_t* zone;
int shm_fd;

// Queues for responses to workers
mqd_t worker_response_queues[NUM_WORKERS];

// Queue for brick input
mqd_t input_queue;

// Queue for communicating with trucks
mqd_t trucks_response_queue;

// This functions opens all necessary queues for IPC
// returns -1 in case of error
int open_queues(mqd_t* conveyor_input_queue, mqd_t* trucks_response_queue, mqd_t* worker_response_queues);

// This function closes all queues
void close_queues(mqd_t conveyor_input_queue, mqd_t trucks_response_queue, mqd_t* worker_response_queues);

// Checks if all the workers have finished (all entries in array are 1)
int all_workers_finished(int* finished_workers);

// Checks if conveyor can fit the new brick
int can_fit_brick(int current_count, int max_count, int current_mass, int max_mass, int brick_mass);

// Signal handler for various signals that cause our process to terminate
void perform_cleanup_and_exit(int a) {
    close_queues(input_queue, trucks_response_queue, worker_response_queues);
    munmap_and_close_shm(shm_fd, zone);
    if(sem_close(zone_sem) < 0) {
        printf("Error while closing semaphore - manual action might be required: %s\n", strerror(errno));
    }

    exit(0);
}

// Conveyor does not need any arguments, only needs to open the shared objects
int main(int argc, char** argv) {
    int res = 0; // result variable for checking errors in library operations

    // Open semaphore used to signal end of loading
    errno = 0;
    zone_sem = sem_open(LOADING_ZONE_SEM_NAME, O_RDWR);
    if(zone_sem == SEM_FAILED) {
        printf("Conveyor error while opening a semaphore: %s\n", strerror(errno));
        exit(1);
    }

    zone = NULL;
    shm_fd = 0;
    if(open_and_map_shm(&shm_fd, &zone) < 0) {
        puts("Conveyor error while opening shared loading zone");
        
        errno = 0;
        if(sem_close(zone_sem) < 0) {
            printf("Error while closing semaphore - manual action might be required: %s\n", strerror(errno));
        }
        exit(1);
    }

    printf("Conveyor mapped the shared loading zone into it's address space at address %p\nValues inside are: Max count = %d, Max mass = %d, Read FD: %d, Write FD: %d\n",
        (void*) zone, zone->max_count, zone->max_mass, zone->read_fd, zone->write_fd);

    res = open_queues(&input_queue, &trucks_response_queue, worker_response_queues);
    if(res < 0) {
        puts("Conveyor error while opening queues, exiting");
        munmap_and_close_shm(shm_fd, zone);
    
        errno = 0;
        if(sem_close(zone_sem) < 0) {
            printf("Error while closing semaphore - manual action might be required: %s\n", strerror(errno));
        }
        exit(1);
    }

    res = install_cleanup_handler(&perform_cleanup_and_exit);
    if(res < 0) {
        puts("Error assigning cleanup handler to signals, exiting");
    
        perform_cleanup_and_exit(0);
    };

    puts("Conveyor ready for work!");

    // Buffer for receiving and sending messages
    message_t msg_recv_buf = { 0 };
    message_t msg_send_buf = { 0 };

    ssize_t nbytes = 0;

    // Keeping track of the number of workers that reported end of work
    int workers_finished[NUM_WORKERS] = { 0 };

    // Perform while loop until all workers finished and the conveyor is empty
    while(!(all_workers_finished(workers_finished) && zone->current_count == 0) || !zone->trucks_stopped) {
        printf("Conveyor waiting for input. Count: %ld/%ld Mass: %ld/%ld\n", zone->current_count, zone->max_count, zone->current_mass, zone->max_mass);

        struct timespec timeout = { 0 };
        clock_gettime(CLOCK_REALTIME, &timeout);
        timeout.tv_sec += 5; // Set timeout to 5 seconds from now

        errno = 0; // Attempt to receive a message
        nbytes = mq_timedreceive(input_queue, (char*) &msg_recv_buf, sizeof(msg_recv_buf), NULL, &timeout);
        if(errno == ETIMEDOUT) {
            puts("Conveyor timed out waiting for a message, re-checking if it should stop");
            continue;
        }
    
        if(nbytes < 0) {
            printf("Conveyor error receiving message: %s\n", strerror(errno));
            perform_cleanup_and_exit(0);
        }

        if(nbytes < sizeof(msg_recv_buf)) {
            puts("Conveyor error receiving message: partial read");
            perform_cleanup_and_exit(0);
        }

        // Possible message: end of work from worker
        if(msg_recv_buf.type == MSG_TYPE_END_OF_WORK) {
            int worker_id = msg_recv_buf.data[0];
            workers_finished[worker_id - 1] = 1;
            printf("Conveyor received end of work from worker id %d - current status: [ %d, %d, %d ]\n", worker_id, workers_finished[0], workers_finished[1], workers_finished[2]);

            // If all workers have stopped, set the flag in the loading zone so the trucks are aware
            if(all_workers_finished(workers_finished)) {
                zone->production_stopped = 1;
            }

            continue;
        }

        // Other possible message: new brick from worker
        if(msg_recv_buf.type == MSG_TYPE_NEW_BRICK) {
            int new_brick_weight = msg_recv_buf.data[0];

            msg_send_buf.type = MSG_TYPE_NEW_BRICK_RESP;

            if(can_fit_brick(zone->current_count, zone->max_count, zone->current_mass, zone->max_mass, new_brick_weight)) {
                printf("Conveyor received brick of mass %d and it can fit!\n", new_brick_weight);
                uint8_t brick = new_brick_weight;
                errno = 0;
                nbytes = write(zone->write_fd, (void*) &brick, sizeof(brick));

                // Check if the actual write happened, and if not, deny the brick
                if(nbytes < sizeof(brick)) {
                    msg_send_buf.status = MSG_DENY;
                    printf("Conveyor error while trying to write to pipe: %s\n", strerror(errno));
                } else {
                    msg_send_buf.status = MSG_APPROVE;
                    (zone->current_count)++;
                    zone->current_mass += new_brick_weight;
                    printf("Conveyor placed brickof mass %d inside the pipe!\n", new_brick_weight);
                }
                
            } else {
                msg_send_buf.status = MSG_DENY;
                printf("Conveyor received brick of mass %d but it does not fit!\n", new_brick_weight);
            }

            // Send response to the worker id that sent us the brick (same as weight)
            errno = 0;
            res = mq_send(worker_response_queues[new_brick_weight - 1], (char*) &msg_send_buf, sizeof(msg_send_buf), 0);
            if(res < 0) {
                printf("Conveyor error sending message: %s\n", strerror(errno));
                perform_cleanup_and_exit(0);
            }

            continue;
        }

        // Last possibility - trucks wants access to the loading zone to load bricks
        if(msg_recv_buf.type == MSG_TYPE_TRUCK_LOADING_REQUEST) {
            msg_send_buf.type = MSG_TYPE_TRUCK_LOADING_RESP;

            // Check if pipe empty, in such case automatically deny loading
            if(zone->current_count == 0) {
                msg_send_buf.status = MSG_DENY;
            } else { // if not empty, give trucks access to loading zone, by waiting for a message from them
                msg_send_buf.status = MSG_APPROVE;
            }

            // send response to the truck
            errno = 0;
            res = mq_send(trucks_response_queue, (char*) &msg_send_buf, sizeof(msg_send_buf), 0);
            if(res < 0) {
                printf("Conveyor error sending message: %s\n", strerror(errno));
                perform_cleanup_and_exit(0);
            }

            // If we sent approval, then wait on the smeaphore before continuing
            if(msg_send_buf.status == MSG_APPROVE) {
                // Wait on the semaphore for the truck to finish loading
                printf("Conveyor received loading request - waiting on the semaphore\n");

                res = sem_wait(zone_sem);
                if(res < 0) {
                    printf("Conveyor error while waiting on the semaphore: %s\n", strerror(errno));
                    perform_cleanup_and_exit(0);
                }
            }

            continue;
        }
    }

    puts("All workers finished, conveyor leaving");

    perform_cleanup_and_exit(0);
}

int can_fit_brick(int current_count, int max_count, int current_mass, int max_mass, int brick_mass) {
    return (current_count < max_count) && ((current_mass + brick_mass) <= max_mass);
}

int open_queues(mqd_t* conveyor_input_queue, mqd_t* trucks_response_queue, mqd_t* worker_response_queues) {
    mqd_t tmp;
    int res = 0;
    errno = 0;
    tmp = mq_open(CONVEYOR_INPUT_QUEUE_NAME, O_RDONLY);
    if(tmp < 0) {
        printf("Conveyor error while opening input queue: %s\n", strerror(errno));
        return -1;
    };

    *conveyor_input_queue = tmp;

    printf("Conveyor opened input queue \"%s\"\n", CONVEYOR_INPUT_QUEUE_NAME);

    errno = 0;
    tmp = mq_open(TRUCKS_RESPONSE_QUEUE_NAME, O_WRONLY);
    if(tmp < 0) {
        printf("Conveyor error while opening trucks response queue: %s\n", strerror(errno));
        res = mq_close(*conveyor_input_queue);

        if(res < 0) {
            printf("conveyor error while closing input queue: %s\n", CONVEYOR_INPUT_QUEUE_NAME, strerror(errno));
        }
        return -1;
    };

    *trucks_response_queue = tmp;

    printf("Conveyor opened trucks response queue \"%s\"\n", TRUCKS_RESPONSE_QUEUE_NAME);

    for(int i = 0; i < NUM_WORKERS; i++) {
        errno = 0;
        tmp = mq_open(common_get_worker_response_queue_name(i + 1), O_WRONLY);

        // check for errors
        if(tmp < 0) {
            printf("Conveyor error while opening worker queue with name \"%s\": %s\n", common_get_worker_response_queue_name(i + 1), strerror(errno));
            int res = 0;

            // If one queue fails, we need to unlink previously created queues
            for(int j = 0; j < i; j++) {
                errno = 0;
                mq_close(worker_response_queues[j]);
            }

            // Also close the one opened previously
            mq_close(*conveyor_input_queue);
            mq_close(*trucks_response_queue);

            // Finally return -1 to report error to the cllign function
            return -1;
        }

        // If succesful, store the queue id in the pointer var
        worker_response_queues[i] = tmp;

        printf("Conveyor opened worker response queue \"%s\"\n", common_get_worker_response_queue_name(i + 1));
    }

    return 0;
}

void close_queues(mqd_t conveyor_input_queue, mqd_t trucks_response_queue, mqd_t* worker_response_queues) {
    errno = 0;
    mq_close(conveyor_input_queue);
    mq_close(trucks_response_queue);

    for(int j = 0; j < NUM_WORKERS; j++) {
        mq_close(worker_response_queues[j]);
    }
}

int all_workers_finished(int* finished_workers) {
    for(int i = 0; i < NUM_WORKERS; i++) {
        if(!finished_workers[i]) return 0;
    }

    return 1;
}
