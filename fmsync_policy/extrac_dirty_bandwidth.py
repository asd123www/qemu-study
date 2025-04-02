import re
import sys
import os
from collections import defaultdict

# Check command line arguments
if len(sys.argv) != 3:
    print(f"Usage: {sys.argv[0]} <directory of policy results> <iter #>")
    sys.exit(1)

directory = sys.argv[1]
specific_iter = int(sys.argv[2])

policy_pattern = re.compile(r'policy:\s*(\S+)')
data_pattern = re.compile(r'iter:\s*(\d+)\s+dirty_set:\s*\d+\s+evict:\s*(\d+)\s+prev_dirty:\s*(\d+)')


policy_data = defaultdict(list)
current_policy = None

for filename in os.listdir(directory):
    if filename.endswith(".txt"):
        filepath = os.path.join(directory, filename)
        with open(filepath, 'r') as file:
            for line in file:
                policy_match = policy_pattern.search(line)
                if policy_match:
                    current_policy = policy_match.group(1)

                data_match = data_pattern.search(line)
                if data_match:
                    iter_num = int(data_match.group(1))
                    if iter_num == specific_iter:
                        evict = data_match.group(2)
                        prev_dirty = data_match.group(3)
                        # print(f"Iter: {iter_num}, Evict: {evict}, Prev_dirty: {prev_dirty}")
                        policy_data[current_policy].append((prev_dirty, evict))


for policy, data_list in policy_data.items():
    data_list.sort()  # Sort by (prev_dirty, evict)
    prev_dirty_list = [str(item[0]) for item in data_list]
    evict_list = [str(item[1]) for item in data_list]
    # reverse those two list
    prev_dirty_list.reverse()
    evict_list.reverse()
    
    print(f"\nPolicy: {policy}")
    print("[" + ", ".join(prev_dirty_list) + "]")
    print("[" + ", ".join(evict_list) + "]")