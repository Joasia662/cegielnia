#include "truck.h"
#include "conveyor.h"

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>

void truck_main() {
    conveyor_t* conveyor = conveyor_attach();
    if (!conveyor) {
        perror("Truck could not attach to conveyor");
        exit(EXIT_FAILURE);
    }

    while (1) {
        brick_t brick = conveyor_remove_brick(conveyor, 3);
        if (brick.mass > 0) {
            printf("[Truck] Picked up brick of weight %d\n", brick.mass);
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
