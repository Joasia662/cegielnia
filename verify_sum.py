import re
import sys

if(len(sys.argv) != 2):
    print("Usage:", sys.argv[0], "[filename]")
    exit(0)

with open(sys.argv[1], 'r') as f:
    txt = f.read()

inserts = re.finditer(r'EVENT_INSERT\(([0-9]+)\)', txt)
removals = re.finditer(r'EVENT_REMOVE\(([0-9]+)\)', txt)

suma = 0

l_ins = 0
for ins in inserts:
    val = int(ins.group(1))
    l_ins += 1
    suma += val

l_rem = 0
for rem in removals:
    val = int(rem.group(1))
    l_rem += 1
    suma -= val

print("With", l_ins, "insertions and", l_rem, "removals, total sum is equal to", suma, "(should be 0)")
print("CORRECT" if suma == 0 else "ERROR")
