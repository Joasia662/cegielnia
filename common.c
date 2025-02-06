#include "common.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include "messages.h"

char* common_get_worker_response_queue_name(int id) {
    if(id < 1 || id > NUM_WORKERS) {
        return NULL;
    }

    char* _queue_names[] = { 
        WORKER_RESPONSE_QUEUE_NAME(1),
        WORKER_RESPONSE_QUEUE_NAME(2),
        WORKER_RESPONSE_QUEUE_NAME(3)
    };

    return _queue_names[id - 1];
}

// Global settings for queues
struct mq_attr _global_queue_attrs = {
    .mq_maxmsg = 10,
    .mq_msgsize = sizeof(message_t)
};

struct mq_attr* get_mq_attrs() {
    return &_global_queue_attrs;
}

int init_shared_loading_zone(int max_count, int max_mass) {
    // Start by opening the shared memory segment
    errno = 0;
    int fd = shm_open(LOADING_ZONE_SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0600);
    if(fd < 0) {
        printf("Error creating shared loading zone: %s\n", errno);
        return -1;
    }

    // Resize it to a size of the structure
    errno = 0;
    if(ftruncate(fd, sizeof(shared_loading_zone_t)) < 0) {
        printf("Error resizing shared loading zone: %s\n", errno);
        if(shm_unlink(LOADING_ZONE_SHM_NAME) < 0) {
            puts("Error unlinking shared memory segment - manual action may be required");
            return -1;
        }
    }

    // Map it to initialize it's values
    errno = 0;
    shared_loading_zone_t* zone = mmap(NULL, sizeof(*zone), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(zone == MAP_FAILED) {
        printf("Error mmaping shared loading zone: %s\n", errno);
        errno = 0;
        if(close(fd) < 0) {
            printf("Error closing shared memory segment during init: %s\n");
        }
        errno = 0;
        if(shm_unlink(LOADING_ZONE_SHM_NAME) < 0) {
            puts("Error unlinking shared memory segment - manual action may be required");
            return -1;
        }
    }

    zone->max_count = max_count;
    zone->current_count = 0;
    zone->max_mass = max_mass;
    zone->current_mass = 0;

    zone->leftover_brick = 0;

    zone->production_stopped = 0;
    zone->trucks_stopped = 0;

    int pipe_fd[2];
    errno = 0;
    if(pipe(pipe_fd) < 0) {
        printf("Error creating a pipe in shared loading zone: %s\n", errno);
        errno = 0;
        if(munmap(zone, sizeof(*zone)) < 0) {
            printf("Error while munmaping shared loading zone: %s\n");
        };
        errno = 0;
        if(close(fd) < 0) {
            printf("Error closing shared memory segment during init: %s\n");
        }
        errno = 0;
        if(shm_unlink(LOADING_ZONE_SHM_NAME) < 0) {
            printf("Error unlinking shared memory segment - manual action may be required: %s\n");
            return -1;
        }
    }
    
    zone->read_fd = pipe_fd[0];
    zone->write_fd = pipe_fd[1];

    // Initialization is done, we can munmap it and close it
    errno = 0;
    if(munmap(zone, sizeof(*zone)) < 0) {
        puts("Error while munmaping shared loading zone: %s\n");
    };

    errno = 0;
    if(close(fd) < 0) {
        printf("Error closing shared memory segment during init: %s\n");
    }

    return 0;
}

int cleanup_shared_loading_zone() {
    errno = 0;
    int fd = shm_open(LOADING_ZONE_SHM_NAME, O_RDWR, 0);
    if(fd < 0) {
        printf("Error opening shared loading zone for cleanup - might require manual action: %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    shared_loading_zone_t* zone = mmap(NULL, sizeof(*zone), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(zone == MAP_FAILED) {
        printf("Error mmaping shared loading zone for cleanup - might require manual action: %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    if(close(zone->read_fd) < 0 || close(zone->write_fd) < 0) {
        printf("Error while cleaning up shared memory (pipe): %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    if(munmap(zone, sizeof(*zone)) < 0) {
        printf("Error while munmaping shared loading zone: %s\n");
    };

    errno = 0;
    if(close(fd) < 0) {
        printf("Error while cleaning up shared memory (shm): %s\n", strerror(errno));
        return -1;
    }

    errno = 0;
    if(shm_unlink(LOADING_ZONE_SHM_NAME) < 0) {
        printf("Error unlinking shared memory segment - manual action may be required: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}

int open_and_map_shm(int* shm_fd, shared_loading_zone_t** shm_zone) {
// Try to open the shared memory
    errno = 0;
    int fd = shm_open(LOADING_ZONE_SHM_NAME, O_RDWR, 0);
    if(fd < 0) {
        printf("Conveyor error opening shared loading zone: %s\n", errno);
        return -1;
    }

    // mmap it into our address space
    errno = 0;
    shared_loading_zone_t* zone = mmap(NULL, sizeof(*zone), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if(zone == MAP_FAILED) {
        printf("Error mmaping shared loading zone: %s\n", errno);
        errno = 0;
        if(close(fd) < 0) {
            printf("Error closing shared memory segment during init: %s\n");
        }

        return -1;
    }

    *shm_fd = fd;
    *shm_zone = zone;

    return 0;
}

void munmap_and_close_shm(int shm_fd, shared_loading_zone_t* zone) {
    errno = 0;
    if(munmap(zone, sizeof(*zone)) < 0) {
        printf("Conveyor error while mumaping shared loading zone: %s\n", strerror(errno));
    }

    errno = 0;
    if(close(shm_fd) < 0) {
        printf("Conveyor error while closing shared loading zone shm fd: %s\n", strerror(errno));
    }
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

int install_cleanup_handler(void (handler)(int)) {
    struct sigaction handler_struct = {
        .sa_handler = handler
    };

    int res = 0;
    res = sigaction(SIGHUP, &handler_struct, NULL);
    if(res != 0) {
        printf("Error: installing SIGHUP cleanup handler: %s\n", strerror(errno));
        return -1;
    }

    res = sigaction(SIGINT, &handler_struct, NULL);
    if(res != 0) {
        printf("Error: installing SIGINT cleanup handler: %s\n", strerror(errno));
        return -1;
    }

    res = sigaction(SIGQUIT, &handler_struct, NULL);
    if(res != 0) {
        printf("Error: installing SIGQUIT cleanup handler: %s\n", strerror(errno));
        return -1;
    }

    res = sigaction(SIGTERM, &handler_struct, NULL);
    if(res != 0) {
        printf("Error: installing SIGTERM cleanup handler: %s\n", strerror(errno));
        return -1;
    }

    res = sigaction(SIGTSTP, &handler_struct, NULL);
    if(res != 0) {
        printf("Error: installing SIGTSTP cleanup handler: %s\n", strerror(errno));
        return -1;
    }

    return 0;
}
