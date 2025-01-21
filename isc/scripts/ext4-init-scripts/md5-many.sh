#!/bin/bash
export DEBUGFS=${DEBUGFS:-/sbin/debugfs}

WORKLOAD="md5"
for size in {256,512,1024,4096}; do
  for count in {500,1000,1500,2000}; do
    export MY_NAME="$WORKLOAD-random-${size}x${count}"

    directory="/$WORKLOAD/$size-x$count"
    bash $(dirname $0)/mkfile-src-dst-sz.sh "$1" /dev/random /$directory/$size.dat $size $count
  done
done