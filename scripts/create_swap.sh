#!/bin/bash
echo "swap space size: $1 M"

sudo mkdir /mnt/ramdisk
sudo mount -t tmpfs -o size="${1}M" tmpfs /mnt/ramdisk
sudo dd if=/dev/zero of=/mnt/ramdisk/swapfile bs=1M count=$1
sudo chmod 600 /mnt/ramdisk/swapfile
sudo mkswap /mnt/ramdisk/swapfile
sudo swapon /mnt/ramdisk/swapfile


# # boot the VM with option:
# -drive file=/mnt/ramdisk/swapfile,format=raw,if=virtio \

# # inside the VM:
# lsblk -f # see the swap device name, default is `vda`
# sudo mkswap /dev/vda
# sudo swapon /dev/vda