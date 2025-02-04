#include "conveyor.h"
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

void worker_main() {
    shared_memory_t* conveyor = conveyor_attach();
    if (!conveyor) {
        perror("Worker could not attach to conveyor");
        exit(EXIT_FAILURE);
    }

    while (1) {
        conveyor_insert_brick(conveyor, 1);
        printf("[Worker] Inserted brick of weight 1\n");
        sleep(1);
    }
}

int main() {
    signal(SIGTERM, exit);
    worker_main();
    return 0;
}
s