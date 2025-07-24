#!/bin/bash
set -euo pipefail
set -x

WKLD_LIST=(a) # b c d e f)
PING_IP=130.127.133.254   # IP to validate VM network
PING_RETRIES=5            # max ping attempts before giving up
MIGRATION_TIMEOUT=60      # seconds to wait for new qemu-system PID

trap 'pkill -f "^promo " 2>/dev/null || true
      screen -wipe >/dev/null 2>&1 || true' EXIT

# ─── Helper functions ──────────────────────────────────────────────────────────
launch_src_vm() {
    local session=$1
    screen -dmS "$session" \
        ./apps/controller shm src apps/vm-boot/redis.exp 4 20G vm_src.txt 500000
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
for wkld in "${WKLD_LIST[@]}"; do
    SRC_SESSION="vm_src_${wkld}"
    DST_SESSION="vm_dst_${wkld}"
    BAK_SESSION="vm_backup_${wkld}"

    # Clean any stale screen sessions
    screen -S "$SRC_SESSION" -X quit 2>/dev/null || true
    screen -S "$DST_SESSION" -X quit 2>/dev/null || true
    screen -S "$BAK_SESSION" -X quit 2>/dev/null || true

    # -------- Launch source VM with network verification ----------------------
    until launch_src_vm "$SRC_SESSION" && wait_for_ping; do
        echo "Ping failed; restarting source VM."
        ./scripts/my_kill.sh
        screen -S "$SRC_SESSION" -X quit 2>/dev/null || true
    done
    src_pid=$(pgrep qemu-system | head -n1)

    # -------- Launch destination VM and wait for migration --------------------
    screen -dmS "$DST_SESSION" \
        ./apps/controller shm dst 4 20G vm_dst.txt 1342177280B

    # -------- Optional backup VM ---------------------------------------------
    screen -dmS "$BAK_SESSION" ./apps/controller shm backup 30

    # -------- Workload --------------------------------------------------------
    ./redis_load.sh "$wkld"
    sleep 30

    { ./redis_run.sh "$wkld" | tee "redis_result_wkld_${wkld}.dat"; } &
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
    sudo ./promo "$vm_pid" /dev/shm/my_shared_memory 1 0 >"/tmp/promo_${wkld}.log" 2>&1 &
    promo_pid=$!

    sleep 300  # workload run time

    # -------- Teardown --------------------------------------------------------
    ./scripts/my_kill.sh
    sudo kill "$promo_pid" 2>/dev/null || true
    wait "$redis_run_pid" 2>/dev/null || true
    sleep 300
done

