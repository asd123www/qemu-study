#!/bin/bash
./load_fm2.sh
./run-redis.sh
./run-redis-hotness-new.sh
./run-redis-hotness-mt.sh
./load_qemu.sh
./run-redis-baseline.sh
./load_fm2.sh

