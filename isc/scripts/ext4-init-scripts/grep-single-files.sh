#!/bin/bash
export DEBUGFS=${DEBUGFS:-/sbin/debugfs}

for size in {4,16,64,256,1024}; do
    size=$(($size << 10))
    export MY_NAME="grep-${size}"
    bash $(dirname $0)/mkfile-ascii-dst-lines.sh "$1" /grep/$size.dat $size 1
done
