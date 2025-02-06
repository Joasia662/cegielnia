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
#include <pthread.h>
#include <semaphore.h>

#include "common.h"
#include "messages.h"

sem_t* zone_sem;
shared_loading_zone_t* zone;
int shm_fd;

// Queue on which messages for the trucks process appear
mqd_t input_queue;

// Queue used to send requests to the conveyor
mqd_t conveyor_input_queue;


// Flag set by signal SIGUSR1 - stops loading and tells the truck to leave early
int truck_leave_early_flag = 0;

// SIGUSR1 tells truck to leave early
void _usr1_handler(int a) {
    truck_leave_early_flag = 1;
}

// Signal handler structure
struct sigaction usr1_sigaction = {
    .sa_handler = &_usr1_handler
};

// This functions opens all necessary queues for IPC
// returns -1 in case of error
int open_queues(mqd_t* input_queue, mqd_t* conveyor_input_queue);

// This function closes all queues
void close_queues(mqd_t input_queue, mqd_t conveyor_input_queue);

void perform_cleanup_and_exit(int a) {
    close_queues(input_queue, conveyor_input_queue);
    munmap_and_close_shm(shm_fd, zone);
    errno = 0;
    if(sem_close(zone_sem) < 0) {
        printf("Error while closing semaphore - manual action might be required: %s\n", strerror(errno));
    }
    exit(0);
}

// structure which contains all data necessary for the threads
struct truck_thread_arg_t {
    shared_loading_zone_t* zone; // SHM loading zone pointer
    sem_t* zone_sem; // Semaphore used to signal to the conveyor that loading is finished

    pthread_mutex_t* mutex; // Mutex locking access to the loading functionality between threads

    int sleep_time_in_seconds; // How much should truck thread sleep after being full
    int capacity; // How much weight of bricks total can a truck carry

    mqd_t conveyor_queue; // Message queue for communicating with conveyor
    mqd_t input_queue; // input to out process, where responses from conveyor will be sent
};
typedef struct truck_thread_arg_t truck_thread_arg_t;

// main function of the threads
void* truck_thread_main(void* args);

// The truck process takes no arguments
int main() {// Open semaphore used to signal end of loading
    int res = 0;

    errno = 0;
    zone_sem = sem_open(LOADING_ZONE_SEM_NAME, O_RDWR);
    if(zone_sem == SEM_FAILED) {
        printf("Conveyor error while opening a semaphore: %s\n", strerror(errno));
        exit(1);
    }

    // Open shared memory segment
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

    printf("Trucks mapped the shared loading zone into it's address space at address %p\nValues inside are: Max count = %d, Max mass = %d, Read FD: %d, Write FD: %d\n",
        (void*) zone, zone->max_count, zone->max_mass, zone->read_fd, zone->write_fd);

    res = open_queues(&input_queue, &conveyor_input_queue);
    if(res < 0) {
        printf("Trucks encountered error while opening queues, exiting");

        errno = 0;
        if(sem_close(zone_sem) < 0) {
            printf("Error while closing semaphore - manual action might be required: %s\n", strerror(errno));
        }
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    }



    errno = 0;
    res = sigaction(SIGUSR1, &usr1_sigaction, NULL);
    if(res != 0) {
        printf("Trucks error: installing SIGUSR1 handler: %s\n", strerror(errno));
        
        perform_cleanup_and_exit(0);
    };

    int defout = dup(1);
    if(defout <0){
        printf("Truck error: can't dump(2): %s\n",  strerror(errno));
        exit(1);
    }

    int file = open( "truck_log.txt", O_WRONLY | O_CREAT, 0600);
    if(file ==-1){
        printf("Truck error: creating log files failed: %s\n",  strerror(errno));
        exit(1);
    }
    int file2 = dup2(file,STDOUT_FILENO);
        if(file2 ==-1){
        printf("Truck error: duplicate a file desriptor failed: %s\n",  strerror(errno));
        exit(1);
    }

    printf("Trucks reporting ready for work! Waiting for signal...\n");

    // Buffer for receiving and sending messages
    message_t msg_recv_buf = { 0 };
    message_t msg_send_buf = { 0 };

    errno = 0;
    ssize_t nbytes = mq_receive(input_queue, (char*) &msg_recv_buf, sizeof(msg_recv_buf), NULL);
    if(nbytes < 0) {
        printf("Trucks error receiving message: %s\n", strerror(errno));
        
        
        perform_cleanup_and_exit(0);
    }

    if(nbytes < sizeof(msg_recv_buf)) {
        fprintf(stderr,"Trucks error receiving message: partial read\n", strerror(errno));
        
        
        perform_cleanup_and_exit(0);
    }

    if(msg_recv_buf.type != MSG_TYPE_SIGNAL_TRUCKS_START) {
        printf("Trucks unexpected message: expected SIGNAL_TRUCKS_START (%d) received: %d\n", MSG_TYPE_SIGNAL_TRUCKS_START, msg_recv_buf.type);
        
        
        perform_cleanup_and_exit(0);
    }

    if(msg_recv_buf.status == MSG_DENY) {
        printf("Trucks received SIGNAL_TRUCKS_START status set to DENY, aborting\n");
        
        
        perform_cleanup_and_exit(0);
    } else if(msg_recv_buf.status != MSG_APPROVE) {
        printf("Trucks received SIGNAL_TRUCKS_START set to invalid status, aborting\n");
        
        
        perform_cleanup_and_exit(0);
    }

    int number_of_truck_threads = msg_recv_buf.data[0];
    int sleep_time_in_seconds = msg_recv_buf.data[1];
    int truck_capacity = msg_recv_buf.data[2];
    printf("Trucks received SIGNAL_TRUCKS_START set to APPROVE, and was given %d threads to spawn, with max cap %d and sleep time %ds, starting work!\n", number_of_truck_threads, truck_capacity, sleep_time_in_seconds);

    // Create mutex used to control which thread has access to loading zone - according to manual, init() always returns 0
    pthread_mutex_t loading_access_mutex;
    pthread_mutex_init(&loading_access_mutex, NULL);

    // Create the args structure
    truck_thread_arg_t thread_arg = {
        .zone = zone,
        .zone_sem = zone_sem,
        .mutex = &loading_access_mutex,
        .sleep_time_in_seconds = sleep_time_in_seconds,
        .capacity = truck_capacity,
        .conveyor_queue = conveyor_input_queue,
        .input_queue = input_queue
    };

    // Spawn worker threads
    pthread_t truck_threads[number_of_truck_threads];
    for(int i = 0; i < number_of_truck_threads; i++) {
        res = pthread_create(&(truck_threads[i]), NULL, &truck_thread_main, (void*) &thread_arg);
        if(res != 0) {
            fprintf(stderr,"Trucks error while creating threads\n");

            for(int j = 0; j < i; j++) {
                pthread_kill(truck_threads[j], SIGTERM);
            };

            perform_cleanup_and_exit(0);
        }
    }

    for(int i = 0; i < number_of_truck_threads; i++) {
        res = pthread_join(truck_threads[i], NULL);
        if(res != 0) {
            fprintf(stderr,"Error joining thread - manual action may be required\n");
        }
    }

    fflush(stdout);
    if(dup2(defout,1)<0){
        printf("Truck error: cannot redirect output back to stdout: %s\n",  strerror(errno));
        exit(1);
    }
    close(file);
    close(file2);
    close(defout);
    
    perform_cleanup_and_exit(0);
}

int open_queues(mqd_t* input_queue, mqd_t* conveyor_input_queue) {
    mqd_t tmp;
    int res = 0;
    errno = 0;
    tmp = mq_open(TRUCKS_RESPONSE_QUEUE_NAME, O_RDONLY);
    if(tmp < 0) {
        printf("Trucks error while opening input queue: %s\n", strerror(errno));
        return -1;
    };

    *input_queue = tmp;

    printf("Trucks opened input queue \"%s\"\n", TRUCKS_RESPONSE_QUEUE_NAME);

    errno = 0;
    tmp = mq_open(CONVEYOR_INPUT_QUEUE_NAME, O_WRONLY);
    if(tmp < 0) {
        printf("Trucks error while opening conveyor input queue: %s\n", strerror(errno));
        errno = 0;
        res = mq_close(*input_queue);

        if(res < 0) {
            printf("conveyor error while closing input queue: %s\n", TRUCKS_RESPONSE_QUEUE_NAME, strerror(errno));
        }
        return -1;
    };

    *conveyor_input_queue = tmp;

    printf("Trucks opened conveyor input queue \"%s\"\n", CONVEYOR_INPUT_QUEUE_NAME);

    

    return 0;
}


void close_queues(mqd_t input_queue, mqd_t conveyor_input_queue) {
    mq_close(input_queue);
    mq_close(conveyor_input_queue);
}

// this function assumes we have exclusive access to loading zone
// acquires bricks until end condition does not happen
// returns 255 if truck needs to leave (full or flag happened)
// returns 0 if conveyor is empty
int truck_thread_load_bricks(truck_thread_arg_t* arg, int* current_mass) {
    while(1) {
        if(arg->zone->current_count <= 0) { // If empty, return 0
            printf("Truck worker found empty queue, leaving\n");
            return 0;
        }

        if(arg->zone->leftover_brick == 0) { // If there is no brick, get next
            errno = 0;
            ssize_t nbytes = read(arg->zone->read_fd, &(arg->zone->leftover_brick), sizeof(arg->zone->leftover_brick));
            if(nbytes < sizeof(arg->zone->leftover_brick)) {
                printf("Error reading next brick: %s\n", strerror(errno));
                pthread_mutex_unlock(arg->mutex);
                pthread_exit(NULL);
            }
        }

        if((arg->zone->leftover_brick + *current_mass > arg->capacity) || truck_leave_early_flag) { // If the brick is too large for us or the flag is set, leave it be and return 255
            printf("Truck leave condition met - flag status: %d\n", truck_leave_early_flag);
            truck_leave_early_flag = 0; // reset the flag, since we're leaving
            return 255;
        }

        // everything else causes us to remove the brick from the conveyor
        *current_mass += arg->zone->leftover_brick;
        arg->zone->current_count -= 1;
        arg->zone->current_mass -= arg->zone->leftover_brick;
        arg->zone->leftover_brick = 0;

        printf("Truck thread mass after loading: %d/%d\n", *current_mass, arg->capacity);
    }
}

void* truck_thread_main(void* argv) {
    truck_thread_arg_t* arg = (truck_thread_arg_t*) argv;
    int res = 0;
    ssize_t nbytes = 0;
    int current_mass = 0; // current mass of bricks carried by the truck

    // Send loading request and receive
    message_t msg_send_buf = { 0 };
    message_t msg_recv_buf = { 0 };

    // this loop tries to gain access to the loading zone
    while(1) {
        // Wait for exclusive access to the loading
        res = pthread_mutex_lock(arg->mutex);
        if(res != 0) {
            printf("Error trucks thread - cant lock mutex\n");
            pthread_exit(NULL);
        }
        puts("Truck thread acquired lock");

        // If production is stopped, and there is no more bricks in conveyor, entire process should exit
        // so we do just that. 
        if(arg->zone->production_stopped && arg->zone->current_count == 0) {
            arg->zone->trucks_stopped = 1; // let conveyor know we stopped
            perform_cleanup_and_exit(0);
        }

        // this loop, having gained exclusive access, waits until conveyor will let us load
        while(1) {
            puts("Truck thread trying to get loading access");

            // Send loading request to conveyor
            msg_send_buf.type = MSG_TYPE_TRUCK_LOADING_REQUEST;

            errno = 0;
            res = mq_send(arg->conveyor_queue, (char*) &msg_send_buf, sizeof(msg_send_buf), 0);
            if(res < 0) {
                printf("Error trucks thread - cant send message: %s\n", strerror(errno));
                pthread_mutex_unlock(arg->mutex);
                pthread_exit(NULL);
            }

            // Receive response from conveyor
            nbytes = mq_receive(arg->input_queue, (char*) &msg_recv_buf, sizeof(msg_recv_buf), NULL);
            if(nbytes < 0) {
                printf("Error trucks thread - cant receive message: %s\n", strerror(errno));
                pthread_mutex_unlock(arg->mutex);
                pthread_exit(NULL);
            }

            // Standard error checking of the response
            if(nbytes < sizeof(msg_recv_buf)) {
                printf("Error trucks thread - partial recv: %s\n", strerror(errno));
                pthread_mutex_unlock(arg->mutex);
                pthread_exit(NULL);
            }

            if(msg_recv_buf.type != MSG_TYPE_TRUCK_LOADING_RESP) {
                printf("Error trucks thread - unexpected message type (expected LOADING_RESP)\n");
                pthread_mutex_unlock(arg->mutex);
                pthread_exit(NULL);
            }

            // Here after all this error checking we know we have an actual response from conveyor
            if(msg_recv_buf.status != MSG_APPROVE) {
                puts("Loading request denied, resetting");
                sleep(1); // If we are denied, it may be because conveyor is empty. Sleep a little and retry
                
                // Sanity check, if we're being denied repeatedly, check if production isnt stoppd
                if(arg->zone->production_stopped) {
                    puts("Truck was being denied, and realised that production has stopped, leaving with what we have");
                    break;
                } else {
                    continue;
                }
            }

            puts("Loading request approved");

            // Here we know we have been approved, now conveyor is waiting for a signal on the zone_semaphore that we have finished

            // the load_bricks function loads bricks until either conveyor is empty, or  truck is full (or receives signal to leave early)
            res = truck_thread_load_bricks(arg, &current_mass);

            // release semaphore since we're done loading
            sem_post(arg->zone_sem);
                
            if(res == 0) {
                printf("Conveyor empty - waiting for it to refill\n");
                sleep(2);
                continue;
            }

            if(res == 255) { // if we're leaving, release semaphore and just break, since we're going away
                printf("Truck leaving - releasing semaphore from truck thread\n");
                break;
            }
        } // end of the loop which tries to make conveyor let us load

        pthread_mutex_unlock(arg->mutex);

        // Go deliver bricks (but only if theres something to deliver - if production stopped we might have left loading empty)
        if(current_mass > 0) {
            printf("EVENT_TRUCK_LEAVE(%d) - Truck thread leaving with %d total mass of bricks\n", current_mass, current_mass);
        
            sleep(arg->sleep_time_in_seconds);
            current_mass = 0;
        }
    } // end of loop that gains exclusive access to the zone

    pthread_exit(NULL);
}
