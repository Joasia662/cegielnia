#include "truck.h"
#include "conveyor.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

void truck_main() {
    shared_memory_t* conveyor = conveyor_attach();
    if (!conveyor) {
        perror("Truck could not attach to conveyor");
        exit(EXIT_FAILURE);
    }

    while (1) {
        int brick_mass = conveyor_remove_brick(conveyor);
        if (brick_mass > 0) {
            printf("[Truck] Picked up brick of weight %d\n", brick_mass);
        } else {
            printf("[Truck] No bricks available\n");
            sleep(1);
        }
    }
}

int main() {
    signal(SIGTERM, exit);
    truck_main();
    return 0;
}
