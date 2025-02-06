import os
import re


def extract_worker_number(log_file):
    match = re.finditer(r'worker(\d+)', log_file) 
    for m in match:
        return m.group(1)
    return None


def check_worker_logs():
    directory = os.path.abspath(os.path.join(os.getcwd()))
    
    log_files = ['worker1.log', 'worker2.log', 'worker3.log']

    for log_file in log_files:
        log_file_path = os.path.join(directory, log_file)
        
        if os.path.exists(log_file_path):           
            with open(log_file_path, 'r') as file:
                content = file.read()
                worker_number = extract_worker_number(log_file)
                # Sprawdzanie, czy EVENT_WORKER_STARTED istnieje bez EVENT_WORKER_FINISHED
                if 'EVENT_WORKER_STARTED' in content and 'EVENT_WORKER_FINISHED' not in content:
                    print(f"ERROR: Worker with id: {worker_number} has started but never finished")
                else:
                    print(f"SUCCESS: Worker with id: {worker_number} has started and finished or has never started")  
        else:       
            print(f"Błąd: Plik {log_file} nie istnieje w katalogu {directory}")

check_worker_logs()