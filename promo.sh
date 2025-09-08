export NTHREADS=1
export BATCH_BYTES=$((1*2048))   # 1 GiB/call ≈ 512 THPs/call
export THROTTLE_MBPS=1024                  # ≈256 THPs/s
export PAUSE_US=1000
export THP_2M_ONLY=1
export HOT_FIRST=1                         # sweep first, then hot

sudo numactl --cpunodebind=0 --membind=0 chrt -i 0 nice -n 19 \
    ./promo_hot_mt $(pgrep qemu) /dev/shm/my_shared_memory 2 0

