#!/bin/bash
export DEBUGFS=${DEBUGFS:-/sbin/debugfs}

for size_log in {8,9,10,12}; do
  size=$((1<<$size_log))
  for count in {500,1000,1500,2000}; do
    export MY_NAME="stats-random-${size}x${count}"

    directory="/stats/$size-x$count"
    bash $(dirname $0)/mkfile-src-dst-sz.sh "$1" /dev/random /$directory/$size.dat $size $count
  done
done
