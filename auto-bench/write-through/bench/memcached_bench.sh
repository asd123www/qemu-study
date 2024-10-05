num_iterations=6

# without migration
for i in $(seq 0 $((num_iterations - 1)))
do
    echo "Running iteration: $i"

    # Dynamically change the file names with the current iteration index
    vm_file="vm_memcached_normal_${i}.txt"
    clt_file="clt_memcached_normal_${i}.txt"

    # Run the python script with the current files and sleep duration of 0
    python3 memcached_write_through.py normal "memcached-experiments/$vm_file" "memcached-experiments/$clt_file" 0
done

# with different sleep time(frequency control).
durations=(1000 50000 100000 200000 300000 400000 500000)
for duration in "${durations[@]}"
do
    ms_duration=$((duration / 1000))

    echo "Running with sleep duration: $duration us"

    for i in $(seq 0 $((num_iterations - 1)))
    do
        # Dynamically change the file names with the appropriate ms value
        vm_file="vm_memcached_shm_${ms_duration}ms_${i}.txt"
        clt_file="clt_memcached_shm_${ms_duration}ms_${i}.txt"

        python3 memcached_write_through.py shm "memcached-experiments/$vm_file" "memcached-experiments/$clt_file" $duration
    done
done