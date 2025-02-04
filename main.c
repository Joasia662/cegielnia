#include "conveyor.h"
#include "worker.h"
#include "truck.h"
#include "sim.h"

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_WORKERS 3
#define MAX_TRUCKS 5

int worker_pids[MAX_WORKERS];
int truck_pids[MAX_TRUCKS];

void stop_workers(int signo) {
    (void)signo; // Ignorowanie parametru, aby uniknąć ostrzeżenia

    for (int i = 0; i < MAX_WORKERS; i++) {
        if (worker_pids[i] > 0) {
            kill(worker_pids[i], SIGTERM);
        }
    }
}

int main() {
    sim_params_t params = {0};
    sim_query_user_for_params(&params);

    shared_memory_t* conveyor = conveyor_init(params.max_bricks_count, params.max_bricks_mass);
    if (!conveyor) {
        perror("Error creating conveyor");
        exit(EXIT_FAILURE);
    }

    signal(SIGUSR2, stop_workers);

    // Start worker processes
    for (int i = 0; i < MAX_WORKERS; i++) {
        if ((worker_pids[i] = fork()) == 0) {
            execl("./worker", "worker", NULL);
            perror("Error starting worker");
            exit(EXIT_FAILURE);
        }
    }

    // Start truck processes
    for (size_t i = 0; i < params.truck_count; i++) { // Poprawione i jako size_t
        if ((truck_pids[i] = fork()) == 0) {
            execl("./truck", "truck", NULL);
            perror("Error starting truck");
            exit(EXIT_FAILURE);
        }
    }

    // Wait for all processes
    for (int i = 0; i < MAX_WORKERS; i++) {
        waitpid(worker_pids[i], NULL, 0);
    }
    for (size_t i = 0; i < params.truck_count; i++) { // Poprawione i jako size_t
        waitpid(truck_pids[i], NULL, 0);
    }

    conveyor_destroy();
    return 0;
}
