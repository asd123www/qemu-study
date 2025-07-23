#!/bin/bash

sudo bash apps/workload_scripts/redis/run_ycsb.sh workload${1} 1 6 5000000 10000000
