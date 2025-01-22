#!/bin/bash

gcc -Wall -Wextra -Werror -pedantic -Wno-error=unused-parameter conveyor.c main.c worker.c truck.c sim.c -o cegielnia
