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

#include "../common.h"
#include "../messages.h"

#define WORKER_ID 3

 // Array of Process IDs of workers
pid_t workers[NUM_WORKERS];

// PID of conveyor process
pid_t conveyor;

// Message queues used to communicate responses to workers
mqd_t worker_response_queues[NUM_WORKERS];

// Message queue used to communicate requests to conveyor
mqd_t conveyor_input_queue;

// Message queue used to communicate responses to loading truck
mqd_t trucks_response_queue;

void perform_cleanup_and_exit(int a) {
    kill(SIGINT, workers[WORKER_ID - 1]);
    kill(SIGINT, conveyor);

    cleanup_queues();
    cleanup_shared_loading_zone();
    sem_unlink(LOADING_ZONE_SEM_NAME);
    exit(0);
}

int main() {
    int conveyor_limit_count = 30;
    int conveyor_limit_mass = 61;

    puts("====== TEST ======");
    puts("This is a test, which verifies whether the limits of conveyor (max mass of bricks, max count of bricks) are preserved.");
    puts("It starts a process for conveyor with a single worker and no trucks. The worker produces bricks of mass 3.");
    printf("The conveyor has limits of %d bricks and %d mass, which means that it should deny worker's bricks before it reaches max count.", conveyor_limit_count, conveyor_limit_mass);
    
    // Despite the fact that only 1 worker is running, we create all queues and shared memory
    // because conveyor expects them to exist

    int res = create_queues(&conveyor_input_queue, &trucks_response_queue, worker_response_queues);
    if(res < 0) {
        puts("Error while creating queues, exiting");\
        exit(1);
    }

    res = init_shared_loading_zone(conveyor_limit_count, conveyor_limit_mass);
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
        perform_cleanup_and_exit(0);
    }

    conveyor = tmp;
    printf("Spawned child conveyor with pid %ld\n", conveyor);

    // Spawn a single worker process with ID 3
    char* argv_worker[] = {"./worker", common_get_worker_response_queue_name(WORKER_ID), NULL};
    tmp = spawn_child("./worker", argv_worker);
    if(tmp < 0) {
        puts("Error while spawning worker");
        perform_cleanup_and_exit(0);
    }

    puts("Installing cleanup signal handlers");
    res = install_cleanup_handler(&perform_cleanup_and_exit);
    if(res < 0) {
        puts("Error assigning cleanup handler to signals, exiting");
    
        perform_cleanup_and_exit(0);
    };

    sleep(3);
    puts("Succesfully spwaned all processes - sending start signal");
    puts("Since there are no trucks running, it is expected that log will soon show new bricks being denied by conveyor after the max mass has been reached");

    // Send WORKER_START to workers
    message_t msg = {
        .type = MSG_TYPE_SIGNAL_WORKER_START,
        .status = MSG_APPROVE
    };

    
    errno = 0;
    msg.data[0] = WORKER_ID;
    res = mq_send(worker_response_queues[WORKER_ID - 1], (char*) &msg, sizeof(msg), 0);
    if(res < 0) {
        printf("Control error sending message: %s\n", strerror(errno));
        perform_cleanup_and_exit(0);
    }

    // Now wait for the worker to finish
    errno = 0;
    if(waitpid(workers[WORKER_ID - 1], NULL, 0) < 0) {
        printf("Error while waiting for worker %d with pid %d: %s\n", WORKER_ID, workers[WORKER_ID - 1], strerror(errno));
    }

    // And the conveyor
    if(waitpid(conveyor, NULL, 0) < 0) {
        printf("Error while waiting for conveyor with pid %d: %s\n", conveyor, strerror(errno));
    }

    cleanup_queues();
    cleanup_shared_loading_zone();
    sem_unlink(LOADING_ZONE_SEM_NAME);
}
