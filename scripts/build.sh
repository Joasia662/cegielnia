gcc conveyor.c common.c -o conveyor
gcc cegielnia.c common.c sim.c -o cegielnia
gcc worker.c common.c -o worker
gcc trucks.c common.c -pthread -o trucks

gcc tests/verify_conveyor_limits.c common.c -o test_verify_conveyor_limits
