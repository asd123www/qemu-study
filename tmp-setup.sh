#!/bin/bash

source config.txt
cd linux-$KERNEL_VER
make defconfig
make kvm_guest.config
CONFIG_KVM_GUEST=y
CONFIG_HAVE_KVM=y
CONFIG_PTP_1588_CLOCK_KVM=y
make olddefconfig
./scripts/config -e MEMCG
make -j 10
cd ..
# creating an image for the kernelPermalink.
sudo apt-get install debootstrap
# cd kernel-image
# chmod +x create-image.sh
# sudo ./create-image.sh
# initialize apps.
cd apps
gcc controller.c -o controller -O3
cd redis
sudo bash setup_redis_client.sh
cd ../../
