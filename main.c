#include "conveyor.h"
#include "worker.h"
#include "truck.h"

#include <stdio.h>
#include <stdlib.h>

// test constants
#define K 100
#define M 200 // 3K > M

#define Nw 3 // number of workers

#define C 50 // Capacity of a truck
#define Nt 10 // Number of trucks
#define Ti 5 // Delivery (sleep) time of a truck in seconds

int main() {
    conveyor_t* conveyor = conveyor_init(K, M);

    if(!conveyor) {
        puts("Error while creating conveyor");
        exit(0);
    }

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

    // Just wait, to be replaced with waiting for threads in the future.
    while(1) {};
}
