#!/bin/bash
NIC_NAME=enp24s0f0

sudo bash scripts/disable_THP.sh always
sudo bash scripts/disable_cpu_scale.sh

# Get IP with CIDR (e.g., 130.127.133.241/22)
ip_cidr=$(ip -4 -o addr show "$NIC_NAME" | awk '{print $4}')

# Get default gateway
gw=$(ip route | awk '/default/ {print $3}')

# Create bridge if it doesn't exist
sudo ip link add br0 type bridge 2>/dev/null || true
sudo ip link set br0 up

# Flush IP from NIC and attach to bridge
sudo ip addr flush dev "$NIC_NAME"
sudo ip link set "$NIC_NAME" master br0
sudo ip link set "$NIC_NAME" up
sudo ip addr add "$ip_cidr" dev br0
sudo ip route add default via "$gw" dev br0 2>/dev/null || true

# tap0 for src, tap1 for dst.
for tap in tap0 tap1; do
    sudo ip tuntap add dev "$tap" mode tap multi_queue 2>/dev/null || true
    sudo ip link set "$tap" up
    sudo ip link set "$tap" master br0
done

echo always | sudo tee /sys/kernel/mm/transparent_hugepage/shmem_enabled
sudo mount -t tmpfs -o remount,rw,nosuid,nodev,huge=always tmpfs /dev/shm
