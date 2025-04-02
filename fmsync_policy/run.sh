#!/bin/bash

if [ "$#" -ne 1 ]; then
    echo "Usage: $0 <dirty log file>"
    exit 1
fi

# Single parameter passed from command line
dirty_log_file="$1"


# Define parameters manually
params=(9000 8500 8000 7500 7000 6500 6000 5500 5000 4500 4000 3500 3000 2500 2000 1500 1000 500 0)

# Define output directory
output_dir="tmp_output"

# Remove existing directory if it exists, then create a new one
if [ -d "$output_dir" ]; then
    rm -rf "$output_dir"
fi
mkdir "$output_dir"

# Iterate over each parameter and run the command, redirecting output
for param in "${params[@]}"
do
    ./policy "$dirty_log_file" "$param" > "$output_dir/${dirty_log_file}_target_${param}.txt"
done

echo "All outputs have been stored in $output_dir"

# python extrac_dirty_bandwidth.py  "$output_dir"