#ifndef _SIM_H_
#define _SIM_H_

#include <stddef.h>

struct sim_params_t {
    size_t max_bricks_count; // K in task description
    size_t max_bricks_mass; // M in task description
    size_t truck_capacity; // C in task description
    size_t truck_count; // N in task description
    unsigned int truck_sleep_time; // Ti in task description
};
typedef struct sim_params_t sim_params_t;

// Ask user about parameters to be used, store them in the structure
void sim_query_user_for_params(sim_params_t*);


#endif