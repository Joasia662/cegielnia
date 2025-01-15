#include "conveyor.h"
#include "worker.h"

#include <stdio.h>
#include <stdlib.h>

// test constants
#define K 100
#define M 200 // 3K > M

#define N 3 // number of workers

int main() {
    conveyor_t* T = conveyor_init(K, M);

    if(!T) {
        puts("Error while creating conveyor");
        exit(0);
    }

    worker_t* P[N];
    for(int i = 0; i < N; i++) {
        P[i] = worker_init(i + 1, i + 1, T);

        if(P[i] == NULL) {
            printf("Error while creating worker with id %d\n", i + 1);
            exit(0);
        }
    };

    for(int i = 0; i < N; i++) {
        int result = worker_start(P[i]);

        if(result == 0) {
            printf("Error while starting worker with id %d\n", P[i]->id);
            exit(0);
        };
    };

    // Just wait, to be rpelaced with waiting for threads in the future.
    while(1) {};
}
