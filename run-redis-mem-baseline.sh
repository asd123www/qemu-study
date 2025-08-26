#!/bin/bash
set -euo pipefail
set -x

WKLD=c
MEM_SIZES=(20G 40G 80G 100G 120G)
PING_IP=10.10.20.254   # IP to validate VM network
PING_RETRIES=5         # max ping attempts before giving up
MIGRATION_TIMEOUT=60   # seconds to wait for new qemu-system PID

trap 'pkill -f "^promo " 2>/dev/null || true
      screen -wipe >/dev/null 2>&1 || true' EXIT

# ─── Helper functions ──────────────────────────────────────────────────────────
launch_src_vm() {
    local session=$1
    local mem=$2
    screen -dmS "$session" \
        ./apps/controller qemu-precopy src apps/vm-boot/redis.exp 4 "$mem" vm_src.txt 500000
}

wait_for_ping() {
    local tries=0
    until ping -c1 -W2 "$PING_IP" >/dev/null 2>&1; do
        ((tries++))
        [[ $tries -ge $PING_RETRIES ]] && return 1
        sleep 20
    done
}

wait_for_migration() {
    local old_pid=$1
    local elapsed=0
    while (( elapsed < MIGRATION_TIMEOUT )); do
        if pgrep qemu-system | grep -v "$old_pid" >/dev/null; then
            return 0
        fi
        sleep 1
        ((elapsed++))
    done
    return 1
}

# ─── Main loop ─────────────────────────────────────────────────────────────────
for mem in "${MEM_SIZES[@]}"; do
    SRC_SESSION="vm_src_${WKLD}_${mem}"
    DST_SESSION="vm_dst_${WKLD}_${mem}"
    BAK_SESSION="vm_backup_${WKLD}_${mem}"

    # Clean any stale screen sessions
    screen -S "$SRC_SESSION" -X quit 2>/dev/null || true
    screen -S "$DST_SESSION" -X quit 2>/dev/null || true
    screen -S "$BAK_SESSION" -X quit 2>/dev/null || true

    # -------- Launch source VM with network verification ----------------------
    until launch_src_vm "$SRC_SESSION" "$mem" && wait_for_ping; do
        echo "Ping failed; restarting source VM."
        ./scripts/my_kill.sh
        screen -S "$SRC_SESSION" -X quit 2>/dev/null || true
    done
    src_pid=$(pgrep qemu-system | head -n1)

    # -------- Launch destination VM and wait for migration --------------------
    screen -dmS "$DST_SESSION" \
        ./apps/controller qemu-precopy dst 4 "$mem" vm_dst.txt 1342177280B
    sleep 10

    # -------- Optional backup VM ---------------------------------------------
    screen -dmS "$BAK_SESSION" bash -c "./apps/controller qemu-precopy backup 100 > precopy_redis_downtime_${WKLD}_${mem}.dat 2>&1"

    # -------- Workload --------------------------------------------------------
    ./redis_load.sh "$WKLD"
    sleep 30

    { ./redis_run.sh "$WKLD" | tee "precopy_redis_perf_${WKLD}_${mem}.dat"; } &
    redis_run_pid=$!
    sleep 30

    [[ -f controller.pid ]] || { echo "controller.pid missing"; ./scripts/my_kill.sh; exit 1; }
    sudo kill -SIGUSR1 "$(cat controller.pid)"

    sleep 40

    if ! wait_for_migration "$src_pid"; then
        echo "Migration timed out."
        ./scripts/my_kill.sh
        continue
    fi

    vm_pid=$(pgrep qemu-system | grep -v "$src_pid" | head -n1)
    [[ -n $vm_pid ]] || { echo "qemu-system PID not found"; ./scripts/my_kill.sh; exit 1; }
    sudo ./promo "$vm_pid" /dev/shm/my_shared_memory 2 0 >"/tmp/promo_${WKLD}_${mem}.log" 2>&1 &
    promo_pid=$!

    sleep 300  # workload run time

    # -------- Teardown --------------------------------------------------------
    ./scripts/my_kill.sh
    sudo kill "$promo_pid" 2>/dev/null || true
    wait "$redis_run_pid" 2>/dev/null || true
    sleep 300
done
