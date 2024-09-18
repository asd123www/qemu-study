source config.txt

if [ $# -lt 2 ]; then
    echo "Error: No parameters provided."
    echo "Usage: $0 <# of threads> <recordcount>"
    exit 1
fi

# the first 
threads=$1
recordcount=$2

cd apps/ycsb

sudo ./bin/ycsb load memcached -s -threads $threads \
                                -P workloads/workloada \
                                -p "memcached.hosts=$VM_IP:12345" \
                                -p "recordcount=$recordcount" \

echo "Finished Memcached loading."