#!/bin/bash

./run-redis-mem.sh
pushd /mnt/nvme2n1/jonggyu/qemu-default/
./setup.sh
popd
./run-redis-mem-baseline.sh
pushd /mnt/nvme2n1/jonggyu/qemu-study/qemu-master/
./setup.sh
popd
