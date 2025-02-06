#ifndef _COMMON_H_
#define _COMMON_H_

#include <string.h>
#include <mqueue.h>
#include <stdint.h>

// Number of workers
#define NUM_WORKERS 3

// Used with worker ID to statically generate queue name for the worker
#define WORKER_RESPONSE_QUEUE_NAME(id) ("/worker_" #id "_response_queue")

// Returns pointer to statically allocated worker response queue name
char* common_get_worker_response_queue_name(int id);

// There is one conveyor and one conveyor input queue, so it's name may be fully static
#define CONVEYOR_INPUT_QUEUE_NAME "/conveyor_input_queue"

// Since there is a single process and only one truck being loaded at any time, there is only one message queue necessary
#define TRUCKS_RESPONSE_QUEUE_NAME "/trucks_response_queue"

#define QUEUE_PERM 0600

// Returns a static instance of mq_attr to be used with queues in simulation
struct mq_attr* get_mq_attrs();

// Name for the posix shared memory segment for shared loading zone
#define LOADING_ZONE_SHM_NAME "/conveyor_loading_zone_shm"

// Name for the posix semaphore which protects access to the loading zone while a truck is loading
#define LOADING_ZONE_SEM_NAME "/conveyor_loading_zone_sem"

// A loading zone for trucks, placed in memory shared between conveyor and trucks
struct shared_loading_zone_t {
    // Counters to keep track of how full the conveyor is
    int max_count;
    int current_count;
    int max_mass;
    int current_mass;

    // Writing and reading FDs for the conveyor storage pipe
    int write_fd;
    int read_fd;

    // Space to hold the next brick, while the truck checks if it has space to load it
    uint8_t leftover_brick;

    // Flag to let trucks know that there will be no more bricks
    int production_stopped;

    // Flag to let conveyor know that trucks stopped
    int trucks_stopped;
};
typedef struct shared_loading_zone_t shared_loading_zone_t;

// Opens a shared memory segment and resizes it. There is no need to store file descriptor,
// since other processes can shm_open it using the known name.
// Returns -1 in case of error
int init_shared_loading_zone(int max_count, int max_mass);

// Performs cleanup on shared loading zone - closes pipe and shm
// Returns -1 in case of error
int cleanup_shared_loading_zone();

// Open and map the shared loading zone
// Returns -1 in case of error
int open_and_map_shm(int* shm_fd, shared_loading_zone_t** zone);

// Unmaps the zone from current process address space, and closes the file descriptor
// If errors occur theres nothing we cna do, so just warn user
void munmap_and_close_shm(int shm_fd, shared_loading_zone_t* zone);

// Spawn a child process with specified arguments
// Returns -1 in case of error, PID of child in case of success
pid_t spawn_child(char* executable_file, char** argv);

// This functions creates all necessary queues for IPC
// returns -1 in case of error
int create_queues(mqd_t* conveyor_input_queue, mqd_t* trucks_response_queue, mqd_t* worker_response_queues);

// This unlinks all the queues
// reports errors to stdout, but there is no way to handle errors anyways, so returns void
void cleanup_queues();

// Installs a single function as a handler for 4 types of signals according to this: https://stackoverflow.com/a/11620170/5457426
// It is meant to clean up resources in a process
int install_cleanup_handler(void (handler)(int));

#endif
