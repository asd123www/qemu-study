source config.txt

if [ $# -lt 2 ]; then
    echo "Error: No parameters provided."
    echo "Usage: $0 <# of threads> <operationcount>"
    exit 1
fi

# the first 
threads=$1
operationcount=$2

cd apps/ycsb

sudo ./bin/ycsb run memcached -s -threads $threads \
                                -P workloads/workloada \
                                -p "memcached.hosts=$VM_IP:12345" \
                                -p status.interval=1 \
                                -p "operationcount=$operationcount" \
                                2>&1

echo "Finished Memcached benchmark."