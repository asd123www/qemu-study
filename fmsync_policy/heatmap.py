import numpy as np
import matplotlib.pyplot as plt

def load_access_data(filename):
    lines = []
    with open(filename, 'r') as f:
        for line in f:
            line = line.strip()
            if line and all(c in '01' for c in line):
                lines.append([int(c) for c in line])
    data_array = np.array(lines, dtype=np.uint8)  # shape: (T, N)
    return data_array.T  # transpose to shape (N, T)

def aggregate_and_scale(data, window_size=10, scale_max=200):
    N, T = data.shape
    print("inside:", N, T, window_size)
    T_new = T // window_size
    data = data[:, :T_new * window_size]  # Trim excess if T is not divisible
    reshaped = data.reshape(N, T_new, window_size)  # shape: (N, T_new, window_size)
    counts = reshaped.sum(axis=2)  # Sum access in each window: shape (N, T_new)
    scaled = counts / window_size * scale_max  # Scale to [0, scale_max]
    return scaled


if __name__ == "__main__":
    filepath = "vm_src_redis_workloadb_50ms.txt"  # replace with your actual filename
    data = load_access_data(filepath)

    N, T = data.shape
    print(f"Original shape: {N} pages x {T} time steps")

    time_window_size = 8  # e.g., aggregate every 10×50ms = 500ms
    heat = aggregate_and_scale(data, window_size = time_window_size, scale_max = 1)
    N, T = heat.shape
    print(f"Aggregated shape: {N} pages x {T} time windows")

    spatial_window_size = 12288 // 256
    heat = aggregate_and_scale(heat.T, window_size = spatial_window_size, scale_max = 200)
    heat = heat.T
    N, T = heat.shape
    print(f"Aggregated shape: {N} pages x {T} time windows")


    plt.figure(figsize=(20, 12))
    plt.imshow(heat, aspect='auto', cmap='plasma', origin='upper')
    plt.colorbar(label='Access Frequency')

    # Optional: format ticks
    tick_spacing_pages = N // 10
    tick_spacing_time = T // 10

    plt.yticks(
        ticks=np.arange(0, N, tick_spacing_pages),
        labels=[f'{i:.1f}' for i in range(0, N, tick_spacing_pages)]
    )
    plt.xticks(
        ticks=np.arange(0, T, tick_spacing_time),
        labels=[f'{i*0.05:.0f}' for i in range(0, T, tick_spacing_time)]
    )

    plt.xlabel("Time (s)")
    plt.ylabel("Guest Mem (GB)")
    plt.title("Page Access Heatmap Over Time")
    plt.tight_layout()
    plt.savefig("heat.jpg")