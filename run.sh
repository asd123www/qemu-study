#!/bin/bash
#rm vm_src.dat vm_dst.dat
#./apps/controller shm src apps/vm-boot/redis.exp 4 20G vm_src.txt 500000 &
#sleep 100
#./apps/controller shm dst 4 20G vm_dst.txt 1342177280B &
#sleep 10
#./apps/controller shm backup 60 &
./redis_load.sh
sleep 30
./redis_run.sh | tee redis_result_pp.dat &
sleep 30
sudo kill -SIGUSR1 "$(cat controller.pid)"

sleep 50

sudo ./promo "$(pgrep qemu-system)" >/tmp/promo.log 2>&1 &

sleep 300
./scripts/my_kill.sh
pkill -ef promo
