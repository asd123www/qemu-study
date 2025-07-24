#!/bin/bash
set -euo pipefail
set -x

WKLD_LIST=(a) # b c d e f)
RATE_LIMIT=(100)
PING_IP=130.127.133.254   # IP to validate VM network
PING_RETRIES=5            # max ping attempts before giving up
MIGRATION_TIMEOUT=60      # seconds to wait for new qemu-system PID
wkld="a"


for rate_limit in "${RATE_LIMIT[@]}"; do
    # ─── Main loop ─────────────────────────────────────────────────────────────────
        ./promo $(pgrep qemu) /dev/shm/my_shared_memory 0 1
        { ./redis_run.sh a | tee "redis_result_wkld_${wkld}_${rate_limit}_hot_norate.dat"; } &
        redis_run_pid=$!
        sleep 30

        sudo ./promo $(pgrep qemu) /dev/shm/my_shared_memory 1 0 >"/tmp/promo_${wkld}.log" 2>&1 &
        promo_pid=$!
        sleep 60  # workload run time
        pkill -ef ycsb || true
        pkill -ef promo || true

       sleep 10
done
