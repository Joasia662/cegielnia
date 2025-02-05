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
