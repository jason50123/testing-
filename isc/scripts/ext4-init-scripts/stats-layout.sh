#!/bin/bash
export DEBUGFS=${DEBUGFS:-/sbin/debugfs}

# 2^12 (4K), 2^14 (16K), 2^16 (64K), 2^18 (256K), 2^20 (1M)
for exp in $(seq 12 2 20); do
  sz=$((1 << $exp))
  export MY_NAME="stats-random-data-$sz"
  bash $(dirname $0)/mkfile-src-dst-sz.sh "$1" /dev/random /stats/$sz.dat $sz
done