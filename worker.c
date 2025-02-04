#include "worker.h"
#include "conveyor.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

void worker_main() {
    conveyor_t* conveyor = conveyor_attach();
    if (!conveyor) {
        perror("Worker could not attach to conveyor");
        exit(EXIT_FAILURE);
    }

    while (1) {
        brick_t brick = { .mass = 1 }; // każdy pracownik ma swoją masę cegieł
        conveyor_insert_brick(conveyor, brick);
        printf("[Worker] Inserted brick of weight %d\n", brick.mass);
        sleep(1);
    }
}

int main() {
    signal(SIGTERM, exit);
    worker_main();
    return 0;
}
