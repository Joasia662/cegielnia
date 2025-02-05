#ifndef _MESSAGES_H_
#define _MESSAGES_H_

#include <stdint.h>

// Mesasge type - to be passed through message queues between processes
struct message_t {
    uint8_t type; // type of message
    uint8_t status; // status, for messages that signify success/failure
    uint8_t data[3]; // some messages require up to 3 additional data arguments
};
typedef struct message_t message_t;

// Type field constants
#define MSG_TYPE_SIGNAL_WORKER_START 1 // Information if the worker process should start work, status is APPROVE / DENY, data[0] is worker id
#define MSG_TYPE_SIGNAL_TRUCKS_START 2 // Information if the trucks process should start work, status is APPROVE / DENY, dat[0] is number of trucks to spawn, data[1] is sleep time in seconds, data[2] is max mass of one truck

#define MSG_TYPE_NEW_BRICK 3 // Worker sends this to conveyor to ask it if it will accept a new brick, data[0] is worker id (so also weight of brick)
#define MSG_TYPE_NEW_BRICK_RESP 4 // Conveyor responds to worker with this, status is APPROVE / DENY, data ignored

#define MSG_TYPE_END_OF_WORK 99 // Worker sends this to conveyor to signify that there will be no more bricks, status is ignored, data[0] is worker id

// status constants
#define MSG_APPROVE 1
#define MSG_DENY 2

#endif
