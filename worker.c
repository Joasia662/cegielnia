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
#include <string.h>

#include "common.h"
#include "messages.h"

// Queue IDs
mqd_t response_queue;
mqd_t conveyor_input_queue;

// Signal handler for sigint etc
void perform_cleanup_and_exit(int a) {
    mq_close(response_queue);
    mq_close(conveyor_input_queue);
    exit(0);
}

// Flag set by signal SIGUSR2 - stops work and exits the process
int worker_stop_flag = 0;

// SIGUSR2 tells workers to stop
void _usr2_handler(int a) {
    worker_stop_flag = 1;
}

// Signal handler structure
struct sigaction usr2_sigaction = {
    .sa_handler = &_usr2_handler
};

void simulate_work() {
    sleep(2);
};

// Worker takes a single argument - queue name for his input
int main(int argc, char** argv) {
    if(argc < 2) {
        printf("Worker PID %d error: not neough arguments, queue name needed\n", getpid());
        exit(1);
    }

    char* queue_name = argv[1];

    int res = 0;
    errno = 0;
    response_queue = mq_open(queue_name, O_RDONLY);
    if(response_queue < 0) {
        printf("Worker PID %d error: cannot open queue \"%s\"\n: %s\n", getpid(), queue_name, strerror(errno));
        exit(1);
    }

    errno = 0;
    conveyor_input_queue = mq_open(CONVEYOR_INPUT_QUEUE_NAME, O_WRONLY);
    if(conveyor_input_queue < 0) {
        printf("Worker PID %d error: cannot open queue \"%s\"\n: %s\n", getpid(), CONVEYOR_INPUT_QUEUE_NAME, strerror(errno));
        mq_close(response_queue);
        exit(1);
    }

    res = install_cleanup_handler(&perform_cleanup_and_exit);
    if(res < 0) {
        printf("Worker PID %d error assigning cleanup handler to signals, exiting\n", getpid());
    
        perform_cleanup_and_exit(0);
    };

    errno = 0;
    res = sigaction(SIGUSR2, &usr2_sigaction, NULL);
    if(res != 0) {
        printf("Worker PID %d error: installing SIGUSR2 handler: %s\n", getpid(), strerror(errno));
        perform_cleanup_and_exit(0);
    };

    printf("Worker PID %d reporting ready for work! Waiting for signal...\n", getpid());

    // Buffer for receiving and sending messages
    message_t msg_recv_buf = { 0 };
    message_t msg_send_buf = { 0 };

    errno = 0;
    ssize_t nbytes = mq_receive(response_queue, (char*) &msg_recv_buf, sizeof(msg_recv_buf), NULL);
    if(nbytes < 0) {
        printf("Worker PID %d error receiving message: %s\n", getpid(), strerror(errno));
        perform_cleanup_and_exit(0);
    }

    if(nbytes < sizeof(msg_recv_buf)) {
        printf("Worker PID %d error receiving message: partial read\n", getpid());
        perform_cleanup_and_exit(0);
    }

    if(msg_recv_buf.type != MSG_TYPE_SIGNAL_WORKER_START) {
        printf("Worker PID %d unexpected message: expected SIGNAL_WORKER_START (%d) received: %d\n", getpid(), MSG_TYPE_SIGNAL_WORKER_START, msg_recv_buf.type);
        perform_cleanup_and_exit(0);
    }

    if(msg_recv_buf.status == MSG_DENY) {
        printf("Worker PID %d received SIGNAL_WORKER_START status set to DENY, aborting\n", getpid());
        perform_cleanup_and_exit(0);
    } else if(msg_recv_buf.status != MSG_APPROVE) {
        printf("Worker PID %d received SIGNAL_WORKER_START set to invalid status, aborting\n", getpid());
        perform_cleanup_and_exit(0);
    }
    
    int worker_id = msg_recv_buf.data[0];

    int defout = dup(1);
    char filename[20];
    snprintf(filename, sizeof(filename), "worker%d.log",worker_id);
    int fd = open( filename, O_WRONLY | O_CREAT, 0600);
    if(fd ==-1){
        printf("Worker error: creating log files failed: %s\n",  strerror(errno));
        exit(1);
    }
    int fd2 = dup2(fd,STDOUT_FILENO);
    if(fd2 ==-1){
        printf("Worker error: duplicate a file desriptor failed: %s\n",  strerror(errno));
        exit(1);
    }
  
    printf("Worker PID %d received SIGNAL_START set to APPROVE, and was assigned worker ID %d, starting work!\n", getpid(), worker_id);
    printf("[P%d] EVENT_WORKER_STARTED", worker_id);
    // worker will produce bricks until he is signaled to stop, by setting the flag up
    while(!worker_stop_flag) {
        //simulate_work(); // Work on the brick for a moment

        msg_send_buf.type = MSG_TYPE_NEW_BRICK;
        msg_send_buf.data[0] = (uint8_t) worker_id;

        // Do while loop of inserting the brick
        do {
            errno = 0; // message conveyor about a new available brick
            res = mq_send(conveyor_input_queue, (char*) &msg_send_buf, sizeof(msg_send_buf), 0);

            if(res < 0) {
                fprintf(stderr, "Worker ID %ld error sending message: %s\n", worker_id, strerror(errno));
                perform_cleanup_and_exit(0);
            }

            errno = 0; // receive acknolwedgement of the brick, and check if it is approval or deny
            nbytes = mq_receive(response_queue, (char*) &msg_recv_buf, sizeof(msg_recv_buf), NULL);
            if(nbytes < 0) {
                fprintf(stderr,"Worker ID %ld error receiving message: %s\n", worker_id, strerror(errno));
                perform_cleanup_and_exit(0);
            }

            if(nbytes < sizeof(msg_recv_buf)) {
                fprintf(stderr,"Worker ID %ld error receiving message: partial read\n", worker_id);
                perform_cleanup_and_exit(0);
            }

            if(msg_recv_buf.type != MSG_TYPE_NEW_BRICK_RESP) {
                fprintf(stderr,"Worker ID %ld unexpected message: expected NEW_BRICK_RESP (%d), received %d\n", worker_id, MSG_TYPE_NEW_BRICK_RESP, msg_recv_buf.type);
                perform_cleanup_and_exit(0);
            }

            sleep(1);
        } while(msg_recv_buf.status != MSG_APPROVE); // Keep reminding conveyor about the new bricks, until it accepts it, but wait a second before each attempt

        printf("[P%d] EVENT_WORKER_INSERT Succesfully inserted brick into conveyor\n", worker_id);
    };

    printf("[P%d] EVENT_WORKER_FINISHED Worker finished and is closing work!\n", worker_id);
    fflush(stdout);
    if(dup2(defout,1)<0){
        fprintf(stderr,"Worker error: cannot redirect output back to stdout: %s\n",  strerror(errno));
        exit(1);
    }
    close(fd);
    close(fd2);
    close(defout);
    // After we finish work, tell conveyor about it

    msg_send_buf.type = MSG_TYPE_END_OF_WORK;
    msg_send_buf.data[0] = (uint8_t) worker_id;

    errno = 0;
    res = mq_send(conveyor_input_queue, (char*) &msg_send_buf, sizeof(msg_send_buf), 0);
    if(res < 0) {
        printf("Worker ID %ld error sending message: %s\n", worker_id, strerror(errno));
        perform_cleanup_and_exit(0);
    }

    perform_cleanup_and_exit(0);
}
