source config.txt

if [ $# -lt 3 ]; then
    echo "Error: No parameters provided."
    echo "Usage: $0 <# of vCPUS> <recordcount> <# of threads>"
    exit 1
fi

# the first 
vCPUs=$1
recordcount=$2
threads=$3

cd apps/ycsb
# sudo ./bin/ycsb load redis -s -P apps/ycsb/workloads/workloada -p "redis.host=$VM_IP" -p "redis.port=12345" &

# Loop from 1 to the number of iterations
for ((i=0; i<vCPUs; i++)); do
    # Calculate 12345 + iteration number
    result=$((12345 + i))

    sudo ./bin/ycsb load redis -s -threads $threads \
                                -P workloads/workloada \
                                -p "redis.host=$VM_IP" \
                                -p "redis.port=$result" \
                                -p "recordcount=$recordcount" \
                                2>&1 | sed "s/^/[client $i] /" &
done

wait

echo "Finished Redis loading."