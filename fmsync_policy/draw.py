import matplotlib.pyplot as plt


def redis_50ms_xl170():
    # Redis-50ms.
    x_lru = [9103, 8662, 8228, 7814, 7383, 6959, 6551, 6126, 5708, 5306, 4886, 4477, 4087, 3680, 3255, 2886, 2499, 2112, 1741]
    y_lru = [103, 162, 228, 314, 383, 459, 551, 626, 708, 806, 886, 977, 1087, 1180, 1255, 1386, 1499, 1612, 1741]

    x_min = [9031, 8548, 8048, 7585, 7106, 6633, 6207, 5707, 5306, 4857, 4383, 3950, 3564, 3076, 2726, 2349, 2027, 1790, 1741]
    y_min = [31, 48, 48, 85, 106, 133, 207, 207, 306, 357, 383, 450, 564, 576, 726, 849, 1027, 1290, 1741]

    x_lfu = [9110, 8673, 8235, 7789, 7343, 6903, 6460, 6021, 5596, 5167, 4737, 4309, 3876, 3442, 3041, 2640, 2277, 1938, 1741]
    y_lfu = [110, 173, 235, 289, 343, 403, 460, 521, 596, 667, 737, 809, 876, 942, 1041, 1140, 1277, 1438, 1741]

    x_lru = list(map(lambda x: x / 9987, x_lru))
    x_min = list(map(lambda x: x / 9987, x_min))
    x_lfu = list(map(lambda x: x / 9987, x_lfu))

    y_lru = list(map(lambda x: x * 2 * 8 / 1000 * 20, y_lru))
    y_min = list(map(lambda x: x * 2 * 8 / 1000 * 20, y_min))
    y_lfu = list(map(lambda x: x * 2 * 8 / 1000 * 20, y_lfu))

    # return x_lru, y_lru, x_min, y_min
    return x_lru, y_lru, x_lfu, y_lfu, x_min, y_min

def redis_100ms_xl170():
    # Redis-100ms.
    x_lru = [9216, 8847, 8500, 8149, 7788, 7433, 7083, 6762, 6413, 6079, 5742, 5401, 5112, 4812, 4504, 4210, 3910, 3601, 3288]
    y_lru = [216, 347, 500, 649, 788, 933, 1083, 1262, 1413, 1579, 1742, 1901, 2112, 2312, 2504, 2710, 2910, 3101, 3288]

    x_min = [9053, 8588, 8126, 7659, 7194, 6782, 6459, 6066, 5590, 5258, 4898, 4464, 4043, 3882, 3575, 3332, 3288, 3288, 3288]
    y_min = [53, 88, 126, 159, 194, 282, 459, 566, 590, 758, 898, 964, 1043, 1382, 1575, 1832, 2288, 2788, 3288]

    x_lfu = [9177, 8795, 8424, 8057, 7693, 7323, 6947, 6592, 6228, 5837, 5479, 5121, 4799, 4447, 4160, 3891, 3629, 3390, 3288]
    y_lfu = [177, 295, 424, 557, 693, 823, 947, 1092, 1228, 1337, 1479, 1621, 1799, 1947, 2160, 2391, 2629, 2890, 3288]

    x_lru = list(map(lambda x: x / 9987, x_lru))
    x_min = list(map(lambda x: x / 9987, x_min))
    x_lfu = list(map(lambda x: x / 9987, x_lfu))

    y_lru = list(map(lambda x: x * 2 * 8 / 1000 * 10, y_lru))
    y_min = list(map(lambda x: x * 2 * 8 / 1000 * 10, y_min))
    y_lfu = list(map(lambda x: x * 2 * 8 / 1000 * 10, y_lfu))

    # return x_lru, y_lru, x_min, y_min
    return x_lru, y_lru, x_lfu, y_lfu, x_min, y_min

def redis_100ms():
    # Redis-100ms.
    x_lru = [9339, 9041, 8695, 8399, 8099, 7813, 7541, 7238, 6953, 6619, 6405, 6194, 5899, 5689, 5442, 5138, 4941, 4660, 4389]
    y_lru = [337, 541, 695, 899, 1099, 1313, 1541, 1738, 1953, 2119, 2405, 2694, 2899, 3189, 3442, 3638, 3941, 4160, 4389]

    x_min = [9090, 8618, 8219, 7811, 7420, 7064, 6610, 6303, 6040, 5540, 5275, 5023, 4830, 4625, 4421, 4408, 4394, 4353, 4337]
    y_min = [90, 118, 219, 311, 420, 564, 610, 803, 949, 1040, 1275, 1523, 1830, 2125, 2421, 2908, 3394, 3853, 4337]

    x_lfu = [9276, 8939, 8618, 8315, 7972, 7630, 7321, 6996, 6700, 6376, 6052, 5733, 5457, 5181, 4951, 4736, 4561, 4407, 4362]
    y_lfu = [276, 439, 618, 815, 972, 1130, 1321, 1496, 1700, 1876, 2052, 2233, 2457, 2681, 2951, 3236, 3561, 3907, 4362]

    x_lru = list(map(lambda x: x / 9987, x_lru))
    x_min = list(map(lambda x: x / 9987, x_min))
    x_lfu = list(map(lambda x: x / 9987, x_lfu))

    y_lru = list(map(lambda x: x * 2 * 8 / 1000 * 10, y_lru))
    y_min = list(map(lambda x: x * 2 * 8 / 1000 * 10, y_min))
    y_lfu = list(map(lambda x: x * 2 * 8 / 1000 * 10, y_lfu))

    # return x_lru, y_lru, x_min, y_min
    return x_lru, y_lru, x_lfu, y_lfu, x_min, y_min

def redis_300ms():
    # Redis-100ms.
    x_lru = [9645, 9504, 9354, 9211, 9101, 9042, 8999, 8870, 8762]
    y_lru = [645, 1004, 1354, 1711, 2101, 2342, 2499, 2870, 3262]

    x_min = [9264, 8897, 8617, 8117, 7617,7582,  7582, 7582, 7527]
    y_min = [264, 397, 617, 617, 617, 882, 1082, 1582, 2027]

    x_lru = list(map(lambda x: x / 9987, x_lru))
    x_min = list(map(lambda x: x / 9987, x_min))

    y_lru = list(map(lambda x: x * 2 * 8 / 300, y_lru))
    y_min = list(map(lambda x: x * 2 * 8 / 300, y_min))

    return x_lru, y_lru, x_min, y_min


def voltdb_200ms():

    x_lru = [4508, 3503, 3004, 2705, 2610, 2563, 2557, 2491, 2488, 2488, 2479]
    y_lru = [8, 3, 4, 5, 10, 63, 157, 191, 288, 488, 679]

    x_min = [4503, 3503, 3003, 2705, 2603, 2540, 2460, 2450, 2450, 2450, 2450]
    y_min = [3, 3, 3, 5, 3, 40, 60, 150, 250, 450, 650]

    x_lru = list(map(lambda x: x / 4551, x_lru))
    x_min = list(map(lambda x: x / 4551, x_min))

    y_lru = list(map(lambda x: x * 2 * 8 / 200, y_lru))
    y_min = list(map(lambda x: x * 2 * 8 / 200, y_min))

    return x_lru, y_lru, x_min, y_min

def voltdb_500ms():

    x_lru = [4510, 4014, 3514, 3015, 2725, 2656, 2653, 2651, 2651, 2649, 2642]
    y_lru = [10, 14, 14, 15, 25, 56, 153, 351, 551, 849, 1142]

    x_min = [4508, 4010, 3509, 3010, 2717, 2629, 2620, 2620, 2620, 2620, 2620]
    y_min = [8, 10, 9, 10, 17, 29, 120, 320, 520, 820, 1120]

    x_lru = list(map(lambda x: x / 4551, x_lru))
    x_min = list(map(lambda x: x / 4551, x_min))

    y_lru = list(map(lambda x: x * 2 * 8 / 500, y_lru))
    y_min = list(map(lambda x: x * 2 * 8 / 500, y_min))

    return x_lru, y_lru, x_min, y_min


x_lru, y_lru, x_lfu, y_lfu, x_min, y_min = redis_50ms_xl170()

# Create the plot
plt.figure(figsize=(8, 5))
plt.plot(x_lru, y_lru, marker='o', label="LRU", linestyle='-', color='blue')
plt.plot(x_lfu, y_lfu, marker='x', label="LFU", linestyle='-')
plt.plot(x_min, y_min, marker='s', label="MIN", linestyle='-', color='orange')


# Labels and title
plt.xlabel("Dirty set size (normalized)")
plt.ylabel("Bandwidth (Gb/s)")
plt.title("redis with 100ms dirty tracking interval")
plt.legend()

# set the x-axis to [0, 1]
plt.xlim(0, 1)

# Show grid
plt.grid(True)

# Display the plot
plt.savefig("line_plot.png")