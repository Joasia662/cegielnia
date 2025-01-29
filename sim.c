#include "sim.h"

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h> // ULONG_MAX definition

#define BUFFER_SIZE 64

void _remove_newline(char* buffer, size_t max_length) {
    size_t len = strnlen(buffer, max_length);

    if(buffer[len - 1] == '\n') {
        buffer[len - 1] = '\0';
    }
}

// Tries to parse a number in buffer using strtoul, returns 0 if it succeeds
int _try_parse_number(char* buffer, unsigned long* result) {
    char* endptr = buffer;
    errno = 0;
    unsigned long result_tmp = strtoul(buffer, &endptr, 10);
    int errno_tmp = errno;

    if(result_tmp == ULONG_MAX || endptr == buffer) {
        if(errno_tmp != 0) { // If errno was set, print message associated with it
            fprintf(stderr, "Error in strtoul(): %s\n", strerror(errno_tmp));
        };
        return 1;
    } else {
        *result = result_tmp;
        return 0;
    }
}

unsigned long _get_number_from_user(char* buffer, size_t max_length) {
    unsigned long result = 0;

    fgets(buffer, max_length, stdin);
    _remove_newline(buffer, max_length);

    if(_try_parse_number(buffer, &result) != 0) {
        fprintf(stderr, "Error - cannot parse input \"%s\" as number\n", buffer);
        exit(0);
    };

    return result;
}

void sim_query_user_for_params(sim_params_t* p) {
    char buffer[BUFFER_SIZE] = { 0 };

    unsigned long current_value = 0;

    fprintf(stderr, "%s\n", "Initialize simulation parameters:");
    fprintf(stderr, "%s\n", "Input the maximum number of bricks in the conveyor (K)");
    fprintf(stderr, "%s\n", "in the range of <3, 5000>:");
    current_value = _get_number_from_user(buffer, BUFFER_SIZE);
    
    if(current_value < 3 || current_value > 5000){
        fprintf(stderr, "Error - input value is outside the range or invalid number. The program is terminated\n");
        exit(0);
    }
    p->max_bricks_count = (size_t) current_value;

    fprintf(stderr, "%s\n", "Input the maximum total mass of bricks in the conveyor (M)");
    fprintf(stderr, "in the range of <6, %lu>:\n", (3 * p->max_bricks_count) - 1); // 3K > M")
    current_value = _get_number_from_user(buffer, BUFFER_SIZE);
    
    if(current_value < 6 || current_value > ((3 * p->max_bricks_count) - 1)){
        fprintf(stderr, "Error - input value is outside the range or invalid number. The program is terminated\n");
        exit(0);
    }
    p->max_bricks_mass = (size_t) current_value;

    fprintf(stderr, "%s\n", "Input maximum total mass of bricks in a single truck (capacity, C)");
    fprintf(stderr, "%s\n", "in the range of <3, 500>:");
    current_value = _get_number_from_user(buffer, BUFFER_SIZE);
    
    if(current_value < 3 || current_value > 500){
        fprintf(stderr, "Error - input value is outside the range or invalid number. The program is terminated\n");
        exit(0);
    }
    p->truck_capacity = (size_t) current_value;

    fprintf(stderr, "%s\n", "Input number of trucks (N)");
    fprintf(stderr, "%s\n", "in the range of <1, 20>:");
    current_value = _get_number_from_user(buffer, BUFFER_SIZE);
    
    if(current_value == 0 || current_value > 20){
        fprintf(stderr, "Error - input value is outside the range or invalid number. The program is terminated\n");
        exit(0);
    }
    p->truck_count = (size_t) current_value;

    fprintf(stderr, "%s\n", "Input truck sleep time (Ti) in seconds");
    fprintf(stderr, "%s\n", "in the range of <1, 20>:");
    current_value = _get_number_from_user(buffer, BUFFER_SIZE);

    if(current_value == 0 || current_value > 20){
        fprintf(stderr, "Error - input value is outside the range or invalid number. The program is terminated\n");
        exit(0);
    }
    p->truck_sleep_time = current_value;

    fprintf(stderr, "%s\n", "simulation summary:");
    fprintf(stderr, "conveyor brick count (K) - %lu\n", p->max_bricks_count);
    fprintf(stderr, "conveyor brick mass (M) - %lu\n", p->max_bricks_mass);
    fprintf(stderr, "truck capacity (C) - %lu\n", p->truck_capacity);
    fprintf(stderr, "truck count (N) - %lu\n", p->truck_count);
    fprintf(stderr, "truck sleep time (Ti) - %u\n", p->truck_sleep_time);
}
