#!/bin/bash
set -x 
for size in 500 
do
    FILE=./my-${size}.dat
    # echo test_resume > /sys/power/disk
    umount /mnt/ramdisk
    mkdir /mnt/ramdisk
    mount -t tmpfs -o size=2G tmpfs /mnt/ramdisk
    dd if=/dev/random of=/mnt/ramdisk/tmp bs=1M count=$size
    echo 1 > /proc/inc_hibernate
    # sleep 6
    # dmesg > $FILE
    # dmesg -c
done