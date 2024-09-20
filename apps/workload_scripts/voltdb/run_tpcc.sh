source config.txt

if [ $# -lt 2 ]; then
    echo "Error: No parameters provided."
    echo "Usage: $0 <duration(seconds)> <# of warehouses>"
    exit 1
fi

# the first
duration=$1
warehouses=$2

cd apps/voltdb/voltdb/tests/test_apps/tpcc
sudo bash run.sh client $duration $warehouses $SHARED_STORAGE/config.txt

echo "Finished VoltDB TPC-C benchmark."