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

// This functions creates all necessary queues for IPC
// returns -1 in case of error
int create_queues(mqd_t* conveyor_input_queue, mqd_t* trucks_response_queue, mqd_t* worker_response_queues);

// This unlinks all the queues
// reports errors to stdout, but there is no way to handle errors anyways, so returns void
void cleanup_queues();

// Spawn a child process with specified arguments
// Returns -1 in case of error, PID of child in case of success
pid_t spawn_child(char* executable_file, char** argv);

int main() {
    // Array of Process IDs of workers
    pid_t workers[NUM_WORKERS];

    // PID of conveyor process
    pid_t conveyor;

    // PID of trucks process
    pid_t trucks;

    // Message queues used to communicate responses to workers
    mqd_t worker_response_queues[NUM_WORKERS];

    // Message queue used to communicate requests to conveyor
    mqd_t conveyor_input_queue;

    // Message queue used to communicate responses to loading truck
    mqd_t trucks_response_queue;

    //get param from the user
    sim_params_t params = { 0 };
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

    sleep(3);
    puts("Succesfully spwaned all processes - sending start signal");

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
            cleanup_queues();
            cleanup_shared_loading_zone();
            sem_unlink(LOADING_ZONE_SEM_NAME);
            exit(1);
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
        cleanup_queues();
        cleanup_shared_loading_zone();
        sem_unlink(LOADING_ZONE_SEM_NAME);
        exit(1);
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

    cleanup_queues();
    cleanup_shared_loading_zone();
    sem_unlink(LOADING_ZONE_SEM_NAME);
}

pid_t spawn_child(char* executable_file, char** argv) {
    errno = 0;
    pid_t res = fork();
    if(res < 0) {
        printf("Error spawning a child \"%s\": %s\n", executable_file, strerror(errno));
        return -1;
    }

    // In parent, returns PID of child
    if(res != 0) {
        return res;
    }

    // In child, returns 0, execute the binary
    errno = 0;
    if(execve(executable_file, argv, NULL) < 0) {
        printf("Child succesfully spawned, but execve returned error: %s\n", strerror(errno));
        exit(1);
    };
}

int create_queues(mqd_t* conveyor_input_queue, mqd_t* trucks_response_queue, mqd_t* worker_response_queues) {
    mqd_t tmp;
    int res = 0;
    errno = 0;
    tmp = mq_open(CONVEYOR_INPUT_QUEUE_NAME, O_CREAT | O_WRONLY | O_EXCL, QUEUE_PERM, get_mq_attrs());
    if(tmp < 0) {
        printf("Error while creating conveyor input queue: %s\n", strerror(errno));
        return -1;
    };

    *conveyor_input_queue = tmp;

    errno = 0;
    tmp = mq_open(TRUCKS_RESPONSE_QUEUE_NAME, O_CREAT | O_WRONLY | O_EXCL, QUEUE_PERM, get_mq_attrs());
    if(tmp < 0) {
        printf("Error while creating trucks response queue: %s\n", strerror(errno));
        res = mq_unlink(CONVEYOR_INPUT_QUEUE_NAME);

        if(res < 0) {
            printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", CONVEYOR_INPUT_QUEUE_NAME, strerror(errno));
        }
        return -1;
    };

    *trucks_response_queue = tmp;

    printf("Succesfully created conveyor input queue \"%s\"\n", CONVEYOR_INPUT_QUEUE_NAME);

    for(int i = 0; i < NUM_WORKERS; i++) {
        errno = 0;
        tmp = mq_open(common_get_worker_response_queue_name(i + 1), O_CREAT | O_WRONLY | O_EXCL, QUEUE_PERM, get_mq_attrs());

        // check for errors
        if(tmp < 0) {
            printf("Error while creating worker queue with name \"%s\": %s\n", common_get_worker_response_queue_name(i + 1), strerror(errno));
            int res = 0;

            // If one queue fails, we need to unlink previously created queues
            for(int j = 0; j < i; j++) {
                errno = 0;
                res = mq_unlink(common_get_worker_response_queue_name(j + 1));
                if(res < 0) {
                    printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", common_get_worker_response_queue_name(j + 1), strerror(errno));
                }
            }

            // Also unlink the one created previously for the conveyor and trucks
            res = mq_unlink(CONVEYOR_INPUT_QUEUE_NAME);
            if(res < 0) {
                printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", CONVEYOR_INPUT_QUEUE_NAME, strerror(errno));
            }

            res = mq_unlink(TRUCKS_RESPONSE_QUEUE_NAME);
            if(res < 0) {
                printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", TRUCKS_RESPONSE_QUEUE_NAME, strerror(errno));
            }

            // Finally return -1 to report error to the cllign function
            return -1;
        }

        // If succesful, store the queue id in the pointer var
        worker_response_queues[i] = tmp;

        printf("Succesfully created conveyor input queue \"%s\"\n", common_get_worker_response_queue_name(i + 1));
    }

    return 0;
}


void cleanup_queues() {
    errno = 0;
    int res = mq_unlink(CONVEYOR_INPUT_QUEUE_NAME);
    if(res < 0) {
        printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", CONVEYOR_INPUT_QUEUE_NAME, strerror(errno));
    } else {
        printf("Succesfully unlinked conveyor input queue \"%s\"\n", CONVEYOR_INPUT_QUEUE_NAME);
    }

    errno = 0;
    res = mq_unlink(TRUCKS_RESPONSE_QUEUE_NAME);
    if(res < 0) {
        printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", TRUCKS_RESPONSE_QUEUE_NAME, strerror(errno));
    } else {
        printf("Succesfully unlinked conveyor input queue \"%s\"\n", TRUCKS_RESPONSE_QUEUE_NAME);
    }

    for(int j = 0; j < NUM_WORKERS; j++) {
        errno = 0;
        res = mq_unlink(common_get_worker_response_queue_name(j + 1));
        if(res < 0) {
            printf("Error while unlinking queue with name \"%s\" - manual removal might be required: %s\n", common_get_worker_response_queue_name(j + 1), strerror(errno));
        } else {
            printf("Succesfully unlinked conveyor input queue \"%s\"\n", common_get_worker_response_queue_name(j + 1));
        }
    }
}
