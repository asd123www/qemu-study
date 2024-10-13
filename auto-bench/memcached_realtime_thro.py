import re
import matplotlib.pyplot as plt
import sys

# Function to extract "27 sec" and "current ops/sec"
def extract_data(line):
    pattern = r"(\d+)\s+sec:.*?(\d+)\s+current ops/sec"
    match = re.search(pattern, line)
    if match:
        return int(match.group(1)), int(match.group(2))  # sec, current ops/sec
    return None, None

def parse_result_file(file_path):
    time_sec = []
    ops_per_sec = []

    with open(file_path, 'r') as file:
        for line in file:
            sec, ops = extract_data(line)
            if sec is not None and ops is not None:
                time_sec.append(sec)
                ops_per_sec.append(ops)

    return time_sec, ops_per_sec

def plot_data(time_sec, ops_per_sec):
    plt.figure(figsize=(20, 12))
    plt.plot(time_sec, ops_per_sec, marker='o', linestyle='-')
    plt.title('Memcached throughput over time')
    plt.xlabel('Time (seconds)')
    plt.ylabel('Ops/sec')
    plt.grid(True)
    plt.show()

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <file_path>")
        sys.exit(1)
    result_file = sys.argv[1]
    
    time_sec, ops_per_sec = parse_result_file(result_file)
    
    if time_sec and ops_per_sec:
        plot_data(time_sec, ops_per_sec)
    else:
        print("No data found in the file.")