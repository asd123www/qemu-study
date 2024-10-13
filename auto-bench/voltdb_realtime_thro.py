import re
import matplotlib.pyplot as plt
import numpy as np
import os
import sys

# Example string
def extract_throughput(text):
    # Regular expression to find the number before "SP/sec"
    match = re.search(r'(\d+(\.\d+)?)\s*SP/sec', text)

    if match:
        sp_sec_value = match.group(1)
        return (True, sp_sec_value)
    
    return (False, False)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python script.py <file_path>")
        sys.exit(1)

    file_path = sys.argv[1]
    thro_lst = []
    with open(file_path, 'r') as file:
        for line in file:
            ret = extract_throughput(line)
            if ret[0] == True:
                thro_lst.append(float(ret[1]))
    # index = next((i for i, num in enumerate(thro_lst) if num > 50000), None)
    # print(index)
    # thro_lst = thro_lst[index + 5: -5]
    print(thro_lst)

    x_ax = [i * 0.2 for i in range(len(thro_lst))]

    plt.figure(figsize=(20, 12))  # Width = 10 inches, Height = 6 inches


    plt.plot(x_ax, thro_lst, marker='o')

    # Adding titles and labels
    plt.title("VoltDB + TPC-C")
    plt.xlabel("time(s)")
    plt.ylabel("tps")
    plt.ylim(bottom=0)

    xticks = [i * 2 for i in range(int(x_ax[-1]) // 2)]  # You can customize this range
    plt.xticks(xticks)  # Set x-ticks
    # plt.savefig('rate_control.jpg', dpi=500)
    plt.show()