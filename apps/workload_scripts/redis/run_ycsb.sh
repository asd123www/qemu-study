source config.txt

if [ $# -lt 5 ]; then
    echo "Error: No parameters provided."
    echo "Usage: $0 <workloada/b/c/d/e/f> <# of vCPUS> <# of threads> <recordcount> <operationcount>"
    exit 1
fi

workload=$1 
vCPUs=$2
threads=$3
recordcount=$4
operationcount=$5

cd apps/ycsb
# sudo ./bin/ycsb load redis -s -P apps/ycsb/workloads/workloada -p "redis.host=$VM_IP" -p "redis.port=12345" &

# Loop from 1 to the number of iterations
for ((i=0; i<vCPUs; i++)); do
    # Calculate 12345 + iteration number
    result=$((12345 + i))

    sudo ./bin/ycsb run redis -s -threads $threads \
                                -P workloads/$workload \
                                -p "redis.host=$VM_IP" \
                                -p "redis.port=$result" \
                                -p "recordcount=$recordcount" \
                                -p "operationcount=$operationcount" \
                                -p status.interval=1 \
                                2>&1 | sed "s/^/[client $i] /" &
done

wait

echo "Finished Redis benchmark."