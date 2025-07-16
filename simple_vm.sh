#!/bin/bash

sudo taskset -c 0-3,16-19 \
qemu-system-x86_64 \
  --enable-kvm \
  -cpu host \
  -smp 4 \
  -m 2048 \
  -object memory-backend-ram,id=mem0,size=1024M,host-nodes=0,policy=bind \
  -object memory-backend-ram,id=mem1,size=1024M,host-nodes=1,policy=bind \
  -numa node,nodeid=0,cpus=0-1,memdev=mem0 \
  -numa node,nodeid=1,cpus=2-3,memdev=mem1 \
  -kernel ./linux-5.15/arch/x86_64/boot/bzImage \
  -nographic -serial mon:stdio \
  -drive file=../shared/kernel-image/bullseye.img,format=raw \
  -append "console=ttyS0 root=/dev/sda earlyprintk=serial net.ifnames=0 numa=on" \
  -netdev tap,id=ens1f0,ifname=tap0,script=no,downscript=no,vhost=on,queues=4 \
  -device virtio-net,netdev=ens1f0,mac=52:55:00:d1:55:01,mq=on,vectors=6 \
  -monitor unix:qemu-monitor-migration-src,server,nowait

