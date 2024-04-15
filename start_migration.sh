#!/bin/bash
source config.txt

# Path to the QEMU monitor Unix socket
MONITOR_SOCKET="./qemu-monitor-migration"

# Use socat to send the command to the QEMU monitor and read the response
echo "migrate_set_capability postcopy-ram on" | socat stdio unix-connect:"$MONITOR_SOCKET"
echo "migrate -d tcp:$DST_IP:$MIGRATION_PORT" | socat stdio unix-connect:"$MONITOR_SOCKET"
echo "migrate_start_postcopy" | socat stdio unix-connect:"$MONITOR_SOCKET"

echo""