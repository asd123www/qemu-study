source config.txt

if [ $# -lt 3 ]; then
    echo "Error: No parameters provided."
    echo "Usage: $0 <workloada/b/c/d/e/f> <# of threads> <operationcount>"
    exit 1
fi

workload=$1
threads=$2
operationcount=$3

cd apps/ycsb

sudo ./bin/ycsb run memcached -s -threads $threads \
                                -P workloads/$workload \
                                -p "memcached.hosts=$VM_IP:12345" \
                                -p status.interval=1 \
                                -p "operationcount=$operationcount" \
                                2>&1

echo "Finished Memcached benchmark."