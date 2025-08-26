#!/usr/bin/env bash
# Copyright 2016 syzkaller project authors. All rights reserved.
set -eux

# Variables affected by options
ARCH=$(uname -m)
RELEASE=bullseye
FEATURE=minimal
SSH_PATH="$HOME"
SEEK=358400
PERF=false
# Create a minimal Debian distribution in a directory.
DIR=chroot
PREINSTALL_PKGS=openssh-server,curl,iptables,pkg-config,libtool,wget,ssh,maven,pdsh,tar,bc,scala,python2,python3,valgrind,ntp,ccache,cmake,locales,memcached,nginx,libjemalloc-dev,libdb++-dev,build-essential,libaio-dev,libnuma-dev,libssl-dev,zlib1g-dev,autoconf,gcc,tmux,vim,automake,libopenmpi-dev,mpich,git,htop,tcpdump,iperf,build-essential,libc6-dev,time,strace,sudo,less,psmisc,selinux-utils,policycoreutils,checkpolicy,selinux-policy-default,firmware-atheros,debian-ports-archive-keyring

# Build a disk image
dd if=/dev/zero of=$RELEASE.img bs=1M seek=$SEEK count=1
sudo mkfs.ext4 -F $RELEASE.img
sudo mkdir -p /mnt/$DIR
sudo mount -o loop $RELEASE.img /mnt/$DIR
sudo cp -a $DIR/. /mnt/$DIR/.
sudo umount /mnt/$DIR
