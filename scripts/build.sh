gcc conveyor.c common.c -o conveyor
gcc cegielnia.c common.c sim.c -o cegielnia
gcc worker.c common.c -o worker
gcc trucks.c common.c -pthread -o trucks
