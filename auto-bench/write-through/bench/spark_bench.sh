durations=(500000)
for duration in "${durations[@]}"
do
    ms_duration=$((duration / 1000))

    echo "Running with sleep duration: $duration us"

    vm_file="vm_spark_shm_${ms_duration}ms.txt"
    clt_file="workload_spark_shm_${ms_duration}ms.txt"

    python3 renew_image.py
    python3 spark_write_through.py shm "spark-experiments/$vm_file" $duration > $clt_file
done

python3 renew_image.py
python3 spark_write_through.py normal vm_spark_normal.txt 0 > workload_spark_normal.txt