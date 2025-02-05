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

#include "common.h"
#include "messages.h"

// Flag set by signal SIGUSR1 - stops loading and tells the truck to leave early
int truck_leve_early_flag = 0;

// SIGUSR1 tells truck to leave early
void _usr1_handler(int a) {
    truck_leve_early_flag = 1;
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

// The truck process takes no arguments
int main() {
    // Open shared memory segment
    shared_loading_zone_t* zone = NULL;
    int shm_fd = 0;
    if(open_and_map_shm(&shm_fd, &zone) < 0) {
        puts("Conveyor error while opening shared loading zone");
        exit(1);
    }

    printf("Trucks mapped the shared loading zone into it's address space at address %p\nValues inside are: Max count = %d, Max mass = %d, Read FD: %d, Write FD: %d\n",
        (void*) zone, zone->max_count, zone->max_mass, zone->read_fd, zone->write_fd);

    // Queue on which messages for the trucks process appear
    mqd_t input_queue;

    // Queue used to send requests to the conveyor
    mqd_t conveyor_input_queue;

    int res = open_queues(&input_queue, &conveyor_input_queue);
    if(res < 0) {
        printf("Trucks encountered error while opening queues, exiting");
        exit(1);
    }

    errno = 0;
    res = sigaction(SIGUSR1, &usr1_sigaction, NULL);
    if(res != 0) {
        printf("Trucks error: installing SIGUSR1 handler: %s\n", strerror(errno));
        close_queues(input_queue, conveyor_input_queue);
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    };

    printf("Trucks reporting ready for work! Waiting for signal...\n");

    // Buffer for receiving and sending messages
    message_t msg_recv_buf = { 0 };
    message_t msg_send_buf = { 0 };

    errno = 0;
    ssize_t nbytes = mq_receive(input_queue, (char*) &msg_recv_buf, sizeof(msg_recv_buf), NULL);
    if(nbytes < 0) {
        printf("Trucks error receiving message: %s\n", strerror(errno));
        close_queues(input_queue, conveyor_input_queue);
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    }

    if(nbytes < sizeof(msg_recv_buf)) {
        printf("Trucks error receiving message: partial read\n", strerror(errno));
        close_queues(input_queue, conveyor_input_queue);
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    }

    if(msg_recv_buf.type != MSG_TYPE_SIGNAL_TRUCKS_START) {
        printf("Trucks unexpected message: expected SIGNAL_TRUCKS_START (%d) received: %d\n", MSG_TYPE_SIGNAL_TRUCKS_START, msg_recv_buf.type);
        close_queues(input_queue, conveyor_input_queue);
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    }

    if(msg_recv_buf.status == MSG_DENY) {
        printf("Trucks received SIGNAL_TRUCKS_START status set to DENY, aborting\n");
        close_queues(input_queue, conveyor_input_queue);
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    } else if(msg_recv_buf.status != MSG_APPROVE) {
        printf("Trucks received SIGNAL_TRUCKS_START set to invalid status, aborting\n");
        close_queues(input_queue, conveyor_input_queue);
        munmap_and_close_shm(shm_fd, zone);
        exit(1);
    }

    int number_of_truck_threads = msg_recv_buf.data[0];
    int sleep_time_in_seconds = msg_recv_buf.data[1];
    int truck_capacity = msg_recv_buf.data[2];
    printf("Trucks received SIGNAL_TRUCKS_START set to APPROVE, and was given %d threads to spawn, with max cap %d and sleep time %ds, starting work!\n", number_of_truck_threads, truck_capacity, sleep_time_in_seconds);

    sleep(10);

    close_queues(input_queue, conveyor_input_queue);
    munmap_and_close_shm(shm_fd, zone);
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
