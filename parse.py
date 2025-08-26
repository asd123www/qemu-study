import os
import re
from collections import defaultdict

def parse_file(filepath):
    """
    Parses a single data file to extract pre-copy duration and VM downtime.

    Args:
        filepath (str): The path to the data file.

    Returns:
        tuple: A tuple containing the migration time and downtime in nanoseconds.
               Returns (None, None) if the data cannot be found.
    """
    migration_time_ns = None
    downtime_ns = None
    try:
        with open(filepath, 'r') as f:
            for line in f:
                if "pre-copy duration:" in line:
                    # Extracts the numeric value from the line
                    migration_time_ns = int(re.search(r'\d+', line).group())
                elif "vm downtime:" in line:
                    # Extracts the numeric value from the line
                    downtime_ns = int(re.search(r'\d+', line).group())
    except (IOError, AttributeError, ValueError) as e:
        print(f"Warning: Could not process file {filepath}. Error: {e}")
        return None, None
    return migration_time_ns, downtime_ns

def main():
    """
    Main function to find files, process them, and print the results.
    """
    # Use a nested defaultdict to easily store the collected data
    # Structure: results[method][workload][size]['migration_times' | 'downtimes'] = [list_of_values]
    results = defaultdict(lambda: defaultdict(lambda: defaultdict(lambda: defaultdict(list))))
    
    current_directory = '.'
    
    # Regex to parse filenames like 'fm2_redis_downtime_c_20G.dat'
    # It captures the method ('fm2' or 'precopy'), workload ('a' or 'c'), and size ('20G')
    filename_pattern = re.compile(r'(fm2|precopy)_redis_downtime_([ac])_(\d+G)\.dat')

    print("--- Starting Analysis ---")
    
    # Walk through all files in the current directory
    for filename in os.listdir(current_directory):
        match = filename_pattern.match(filename)
        if match:
            method, workload, size = match.groups()
            filepath = os.path.join(current_directory, filename)
            
            print(f"Processing file: {filename}")
            migration_time, downtime = parse_file(filepath)
            
            if migration_time is not None and downtime is not None:
                results[method][workload][size]['migration_times'].append(migration_time)
                results[method][workload][size]['downtimes'].append(downtime)

    print("\n--- Analysis Complete ---")
    
    # --- Print Results in CSV format ---
    print("\n" + "="*50)
    print("      Migration & Downtime Analysis Results (CSV)")
    print("="*50)

    # Print CSV header
    print("Method,Workload,Size,MigrationTime(s),Downtime(s)")

    # Get a sorted list of all unique memory sizes found
    all_sizes = sorted(
        list(set(size for method_data in results.values() for workload_data in method_data.values() for size in workload_data.keys())),
        key=lambda s: int(s.replace('G', ''))
    )
    
    # Get the workloads that are actually present in the data
    found_workloads = set(workload for method_data in results.values() for workload in method_data.keys())
    
    # Define the desired order for workloads and methods
    workload_order = [w for w in ['a', 'c'] if w in found_workloads]
    method_order = ['fm2', 'precopy']

    if not all_sizes:
        print("\nNo matching data files found in the current directory.")
        print("Please ensure files are named like 'fm2_redis_downtime_a_20G.dat' or '..._c_20G.dat'")
        return

    # Iterate through workloads, then methods, then sizes to achieve the desired print order
    for workload in workload_order:
        for method in method_order:
            for size in all_sizes:
                data = results[method][workload][size]
                
                if data['migration_times'] and data['downtimes']:
                    # Calculate the average and convert from nanoseconds to seconds
                    avg_migration_s = sum(data['migration_times']) / len(data['migration_times']) / 1_000_000_000
                    avg_downtime_s = sum(data['downtimes']) / len(data['downtimes']) / 1_000_000_000
                    
                    # Print data row in CSV format
                    print(f"{method},{workload},{size},{avg_migration_s:.2f},{avg_downtime_s:.2f}")

    print("\n" + "="*50)


if __name__ == "__main__":
    main()


