#!/bin/bash
./load_fm2.sh origin/jonggyu
./run-redis.sh
./load_fm2.sh origin/multi-threaded
./run-redis-syncmt.sh
./run-redis-syncmt-hotmt.sh
./load_qemu.sh
./run-redis-baseline.sh
./load_fm2.sh origin/multi-threaded
