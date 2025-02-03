This is a multi-threaded simulation of a brickyard, where three workers (marked as P1, P2 and P3) work on a conveyor belt, and each of them produces and throws bricks onto the belt, weighing 1, 2 and 3 units, respectively.

Using the conveyor structure (production belt), bricks are delivered to trucks, observing the requirements set in the task:
• maximum number of bricks on the belt (K)
• belt capacity (M)
• belt capacity does not exceed the set value (3K<M)
At the same time, bricks "ride" off the belt onto the truck in exactly the same order as they were placed on the belt. All trucks have the same capacity C specified by the user. After the truck is full, it goes to transport the load. To simulate this, the truck thread goes to sleep (sleep () function) for a number of seconds defined by the user. A new truck appears in its place immediately, if available. Each employee and each truck is a separate thread.
Employees create bricks endlessly and trucks deliver them endlessly. To stop the simulation, a distributor command is needed, which is simulated by sending the USR2 signal to the process. It signals the end of production (setting the appropriate flag).

To test the correctness of the code, two tests were created:
• The first one (verify_sum.py) accepts application logs and, by analyzing events in the conveyor module, checks whether the number of bricks entered equals the sum of bricks output.

• The second one (check_stats.py), by analyzing logs from the worker and truck modules, displays how much work each worker and truck did. For example, this allows us to determine which thread used the conveyor module resources more often. Additionally, we check whether the sum of the masses of bricks that were produced and those that were taken away by trucks matches.

Data for simulation can be entered by the user, or saved as a text file and loaded into the program by redirecting the standard input from the file in the terminal when starting the simulation. Similarly, it is possible to create a file with logs for testing purposes by redirecting the standard output to a file in the terminal.

The resulting project is divided into modules:
• sim: short for "simulation" - accepts simulation parameters from standard input and checks whether they fit within reasonable constraints
• to the worker: implements the logic of the worker thread and stops flag handling
• truck: implements the logic of the truck thread
• conveyor: implements the logic of the conveyor structure along with synchronization between threads and exposes ready-made functions for use by trucks and workers
• main: is the program's entry point, initializes simulations and handles signal handling
