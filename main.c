#include "conveyor.h"
#include "worker.h"
#include "truck.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>

// test constants
#define K 100
#define M 200 // 3K > M

#define Nw 3 // number of workers

#define C 50 // Capacity of a truck
#define Nt 10 // Number of trucks
#define Ti 5 // Delivery (sleep) time of a truck in seconds


// SIGUSR2 tells workers to stop
void _usr2_handler(int a) {
    worker_stop_flag_set();
}

struct sigaction usr2_sigaction = {
    .sa_handler = &_usr2_handler
};

int main() {
    conveyor_t* conveyor = conveyor_init(K, M);

    if(!conveyor) {
        puts("Error while creating conveyor");
        exit(0);
    }

    // Block all signals prior to creating worker threads, as htey will inherit the signal mask
    // And we only want to receive signals in the main thread
    sigset_t set;
    sigfillset(&set); // select all signals (fill the set)
    int result = pthread_sigmask(SIG_BLOCK, &set, NULL); // Set all signals to block
    if(result != 0) {
        conveyor_destroy(conveyor);
        puts("Error while  setting sigmask");
        exit(0);
    };

    worker_t* workers[Nw];
    for(int i = 0; i < Nw; i++) {
        workers[i] = worker_init(i + 1, i + 1, conveyor);

        if(workers[i] == NULL) {
            printf("Error while creating worker with id %d\n", i + 1);
            exit(0);
        }
    };

    for(int i = 0; i < Nw; i++) {
        int result = worker_start(workers[i]);

        if(result == 0) {
            printf("Error while starting worker with id %d\n", workers[i]->id);
            exit(0);
        };
    };

    truck_t* trucks[Nt];
    for(int i = 0; i < Nt; i++) {
        trucks[i] = truck_init(i + 1, C, Ti, conveyor);

        if(trucks[i] == NULL) {
            printf("Error while creating truck with id %d\n", i + 1);
            exit(0);
        }
    };

    for(int i = 0; i < Nt; i++) {
        int result = truck_start(trucks[i]);

        if(result == 0) {
            printf("Error while starting truck with id %d\n", trucks[i]->id);
            exit(0);
        };
    };

    result = sigaction(SIGUSR2, &usr2_sigaction, NULL);
    if(result != 0) {
        puts("Error installing SIGUSR2 handler");
        exit(0);
    };

    // Unblock the signals after creating workers and installing signal handlers
    pthread_sigmask(SIG_UNBLOCK, &set, NULL); // Set all signals to unblock

    // Join threads
    for(int i = 0; i < Nw; i++) {
        pthread_join(workers[i]->thread_id, NULL);
        printf("[Main] Finished waiting for worker %d\n", workers[i]->id);
    };


    for(int i = 0; i < Nt; i++) {
        pthread_join(trucks[i]->thread_id, NULL);
        printf("[Main] Finished waiting for truck %d\n", trucks[i]->id);
    };

    conveyor_destroy(conveyor);
}
