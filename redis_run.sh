#!/bin/bash

sudo bash apps/workload_scripts/redis/run_ycsb.sh workload${1} 1 20 5000000 100000000
