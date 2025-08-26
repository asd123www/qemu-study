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
    # Structure: results[method][size]['migration_times' | 'downtimes'] = [list_of_values]
    results = defaultdict(lambda: defaultdict(lambda: defaultdict(list)))
    
    current_directory = '.'
    
    # Regex to parse filenames like 'fm2_redis_downtime_a_20G.dat'
    # It captures the method ('fm2' or 'precopy') and the size ('20G')
    filename_pattern = re.compile(r'(fm2|precopy)_redis_downtime_a_(\d+G)\.dat')

    print("--- Starting Analysis ---")
    
    # Walk through all files in the current directory
    for filename in os.listdir(current_directory):
        match = filename_pattern.match(filename)
        if match:
            method, size = match.groups()
            filepath = os.path.join(current_directory, filename)
            
            print(f"Processing file: {filename}")
            migration_time, downtime = parse_file(filepath)
            
            if migration_time is not None and downtime is not None:
                results[method][size]['migration_times'].append(migration_time)
                results[method][size]['downtimes'].append(downtime)

    print("\n--- Analysis Complete ---")
    
    # --- Print Results ---
    print("\n" + "="*50)
    print("      Migration & Downtime Analysis Results")
    print("="*50)

    # Get a sorted list of all unique memory sizes found (e.g., ['20G', '40G', ...])
    all_sizes = sorted(
        list(set(size for res in results.values() for size in res.keys())),
        key=lambda s: int(s.replace('G', ''))
    )

    if not all_sizes:
        print("\nNo matching data files found in the current directory.")
        print("Please ensure files are named like 'fm2_redis_downtime_a_20G.dat'")
        return

    for size in all_sizes:
        print(f"\n--- Results for Memory Size: {size} ---")
        
        for method in ['precopy', 'fm2']:
            data = results[method][size]
            
            if data['migration_times'] and data['downtimes']:
                # Calculate the average if multiple runs exist (currently handles one)
                avg_migration_ms = sum(data['migration_times']) / len(data['migration_times']) / 1_000_000
                avg_downtime_ms = sum(data['downtimes']) / len(data['downtimes']) / 1_000_000
                
                print(f"  Method: {method.upper()}")
                print(f"    - Average Migration Time: {avg_migration_ms:.2f} ms")
                print(f"    - Average VM Downtime:    {avg_downtime_ms:.2f} ms")
            else:
                print(f"  Method: {method.upper()}")
                print("    - No data found.")

    print("\n" + "="*50)


if __name__ == "__main__":
    main()

