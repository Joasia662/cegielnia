#include <fcntl.h>
#include <sys/stat.h>
#include <mqueue.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <semaphore.h>

#include "common.h"
#include "messages.h"
#include "sim.h"

// Array of Process IDs of workers
pid_t workers[NUM_WORKERS];

// PID of conveyor process
pid_t conveyor;

// PID of trucks process
pid_t trucks;

// Simulation parameters
sim_params_t params = { 0 };

// Signal handler for various signals that cause our process to terminate
void perform_cleanup_and_exit(int a) {
    cleanup_queues();
    cleanup_shared_loading_zone();
    sem_unlink(LOADING_ZONE_SEM_NAME);

    for(int i = 0; i < NUM_WORKERS; i++) {
        kill(workers[i], SIGINT);
    }

    kill(trucks, SIGINT);
    kill(conveyor, SIGINT);

    exit(1);
}

int main() {
    // Message queues used to communicate responses to workers
    mqd_t worker_response_queues[NUM_WORKERS];

    // Message queue used to communicate requests to conveyor
    mqd_t conveyor_input_queue;

    // Message queue used to communicate responses to loading truck
    mqd_t trucks_response_queue;

    //get param from the user
    sim_query_user_for_params(&params);

    int res = create_queues(&conveyor_input_queue, &trucks_response_queue, worker_response_queues);
    if(res < 0) {
        puts("Error while creating queues, exiting");\
        exit(1);
    }

    res = init_shared_loading_zone(params.max_bricks_count, params.max_bricks_mass);
    if(res < 0) {
        puts("Error while initializing shared loading zone");
        cleanup_queues();
        exit(1);
    }

    // Create semaphore with value zero, used to signal between trucks and conveyor
    errno = 0;
    sem_t* sem = sem_open(LOADING_ZONE_SEM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600, 0);
    if(sem == SEM_FAILED) {
        printf("Error while creating a semaphore: %s\n", strerror(errno));
        cleanup_queues();
        exit(1);
    }

    errno = 0;
    if(sem_close(sem) < 0) {
        printf("Error while closing semaphore - manual action might be required: %s\n", strerror(errno));
    }

    // Spawn conveyor process
    char* argv_conv[] = {"./conveyor", NULL};
    pid_t tmp = spawn_child("./conveyor", argv_conv);
    if(tmp < 0) {
        puts("Error while spawning conveyor");
        cleanup_queues();
        cleanup_shared_loading_zone();
        sem_unlink(LOADING_ZONE_SEM_NAME);
        exit(1);
    }

    conveyor = tmp;
    printf("Spawned child conveyor with pid %ld\n", conveyor);

    // Spawn trucks process
    char* argv_trucks[] = {"./trucks", NULL};
    tmp = spawn_child("./trucks", argv_trucks);
    if(tmp < 0) {
        puts("Error while spawning trucks");
        cleanup_queues();
        cleanup_shared_loading_zone();
        sem_unlink(LOADING_ZONE_SEM_NAME);
        exit(1);
    }

    trucks = tmp;
    printf("Spawned child trucks with pid %ld\n", trucks);

    // Spawn worker processes
    for(int i = 0; i < NUM_WORKERS; i++) {
        char* argv_worker[] = {"./worker", common_get_worker_response_queue_name(i + 1), NULL};
        tmp = spawn_child("./worker", argv_worker);

        if(tmp < 0) {
            printf("Error while spawning worker id %d\n", i + 1);

            errno = 0;
            res = kill(conveyor, SIGTERM);
            if(res < 0) {
                printf("Error while trying to kill child with pid %d - manual intervention may be required: %s\n", conveyor, strerror(errno));
            }

            errno = 0;
            res = kill(trucks, SIGTERM);
            if(res < 0) {
                printf("Error while trying to kill child with pid %d - manual intervention may be required: %s\n", trucks, strerror(errno));
            }

            for(int j = 0; j < i; j++) {
                errno = 0;
                res = kill(workers[j], SIGTERM);
                if(res < 0) {
                    printf("Error while trying to kill child with pid %d - manual intervention may be required: %s\n", workers[j], strerror(errno));
                }
            }

            cleanup_queues();
            cleanup_shared_loading_zone();
            sem_unlink(LOADING_ZONE_SEM_NAME);
            exit(1);
        }

        workers[i] = tmp;
        printf("Spawned child worker with pid %ld\n", workers[i]);
    }

    puts("Installing cleanup signal handlers");
    res = install_cleanup_handler(&perform_cleanup_and_exit);
    if(res < 0) {
        puts("Error assigning cleanup handler to signals, exiting");
    
        perform_cleanup_and_exit(0);
    };

    sleep(3);
    puts("Succesfully spawned all processes - sending start signal");

    // Send WORKER_START to workers
    message_t msg = {
        .type = MSG_TYPE_SIGNAL_WORKER_START,
        .status = MSG_APPROVE
    };

    for(int i = 0; i < NUM_WORKERS; i++) {
        errno = 0;
        msg.data[0] = i + 1;
        res = mq_send(worker_response_queues[i], (char*) &msg, sizeof(msg), 0);
        if(res < 0) {
            printf("Control error sending message: %s\n", strerror(errno));
            perform_cleanup_and_exit(0);
        }
    }

    // Send TRUCKS_START to trucks
    msg.type = MSG_TYPE_SIGNAL_TRUCKS_START;
    msg.data[0] = params.truck_count;
    msg.data[1] = params.truck_sleep_time;
    msg.data[2] = params.truck_capacity;

    res = mq_send(trucks_response_queue, (char*) &msg, sizeof(msg), 0);
    if(res < 0) {
        printf("Control error sending message: %s\n", strerror(errno));
        perform_cleanup_and_exit(0);
    }

    // Now wait for the workers to finish
    for(int i = 0; i < NUM_WORKERS; i++) {
        errno = 0;
        if(waitpid(workers[i], NULL, 0) < 0) {
            printf("Error while waiting for worker %d with pid %d: %s\n", i + 1, workers[i], strerror(errno));
        }
    }

    // And the conveyor
    if(waitpid(conveyor, NULL, 0) < 0) {
        printf("Error while waiting for conveyor with pid %d: %s\n", conveyor, strerror(errno));
    }

    // And the trucks
    if(waitpid(trucks, NULL, 0) < 0) {
        printf("Error while waiting for trucks with pid %d: %s\n", trucks, strerror(errno));
    }

    perform_cleanup_and_exit(0);
}

