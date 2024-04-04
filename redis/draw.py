import matplotlib.pyplot as plt
import numpy as np

# read data.
time = []
latency = []
# Open the file for reading
with open('example.txt', 'r') as file:
    # Iterate over each line in the file
    for line in file:
        # Split the line into parts using space (or another delimiter) as the separator
        parts = line.split()
        
        # Convert the parts to floats and add them as a tuple to the data list
        # This assumes each line correctly contains two float numbers
        if len(parts) == 2:
            time.append(float(parts[0]) / 1000000000.0)
            latency.append(float(parts[1]) / 1000.0)

# time = np.linspace(min(), 100, 500)  # Simulating time from 1 to 100 seconds
# latency = np.random.normal(loc=100, scale=20, size=500)  # Generating random latencies
print(max(latency))

plt.figure(figsize=(10, 6))
plt.scatter(time, latency, label='Latency', color='blue')
# plt.plot(time, latency, label='Latency', color='blue')
plt.title('Redis Latency over Time')
plt.xlabel('Time (seconds)')
plt.ylabel('Latency (us)')
plt.legend()
plt.grid(True)

plt.savefig('latency_graph_migrate.png', dpi=500)  

# plt.show()
