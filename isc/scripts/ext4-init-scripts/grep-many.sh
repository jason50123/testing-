#!/bin/bash
export DEBUGFS=${DEBUGFS:-/sbin/debugfs}

for size in {256,512,1024,4096}; do
  export MY_NAME="grep-${size}"
  for count in {500,1000,1500,2000}; do
      pattern=$'\npatternforISCgreptask\n'
      directory="/grep/$size-x$count"
      bash $(dirname $0)/mkfile-src-dst-sz.sh "$1" /dev/random /$directory/$size.dat $size $count "$pattern"
  done
done
