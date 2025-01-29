import re
import sys

if(len(sys.argv) != 2):
    print("Usage:", sys.argv[0], "[filename]")
    exit(0)

with open(sys.argv[1], 'r') as f:
    log = f.read()

all_trucks_start_events = re.finditer(r'EVENT_TRUCK_START\(([0-9]+)\)', log)
truck_dict_removals = {}
for t in all_trucks_start_events:
    idx = int(t.group(1))
    truck_dict_removals[idx] = {
        'total_count': 0,
        'total_mass': 0
    }

all_removal_events = re.finditer(r'EVENT_TRUCK_REMOVAL\(([0-9]+),([0-9]+)\)', log)
for removal in all_removal_events:
    truck_id = int(removal.group(1))
    brick_size = int(removal.group(2))

    truck_dict_removals[truck_id]['total_count'] += 1
    truck_dict_removals[truck_id]['total_mass'] += brick_size

worker_dict_insertions = {
    1: 0,
    2: 0,
    3: 0
}

all_insertion_events = re.finditer(r'EVENT_WORKER_INSERT\(([0-9]+)\)', log)
for insertion in all_insertion_events:
    worker_id = int(insertion.group(1))
    worker_dict_insertions[worker_id] += 1 # same as weight

sum_inserted_bricks = 0
for worker in worker_dict_insertions:
    worker_sum = worker_dict_insertions[worker]*worker
    print(f"Worker with id {worker} has put {worker_dict_insertions[worker]} bricks on the conveyor. The total mass is {worker_sum}" )
    sum_inserted_bricks += worker_sum
print("\n")
sum_received_bricks = 0
for truck in truck_dict_removals:
    print(f"Truck with id {truck} has received {truck_dict_removals[truck]['total_count']} bricks from the conveyor. The total mass is {truck_dict_removals[truck]['total_mass']}" )
    sum_received_bricks += truck_dict_removals[truck]['total_mass']

print("SUM OF MASS IS CORRECT" if sum_received_bricks == sum_inserted_bricks else "ERROR")