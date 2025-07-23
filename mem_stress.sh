#!/bin/bash
numactl --physcpubind=20,22,24,26,28,30,1,3,5,7,9,11,13,15 --membind=0,1 \
  stress-ng --memrate 12 --memrate-bytes 10G --timeout 10m
