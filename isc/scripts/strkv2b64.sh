#!/bin/bash
set -x
tmpFile=/tmp/$(basename $(realpath $0)).bin
keyLength=32
valLengthMax=4096
valLength=$(( ${#2} > $valLengthMax ? $valLengthMax : ${#2} ))

# check inputs
if [[ $# != 2 ]]; then
  echo "Expect 2 arguments, but got $#";
  echo "Usage: ./program \$keyString \$valString";
  exit -1
fi

srcKeyLength=${#1}
srcValLength=${#2}
padKeyLength=$(($keyLength - $srcKeyLength))
padValLength=$(($valLength - $srcValLength))

echo -n $1 > $tmpFile
head -c $padKeyLength /dev/zero >> $tmpFile
echo -n $2 >> $tmpFile
head -c $padValLength /dev/zero >> $tmpFile

# xxd $tmpFile

base64 -w 0 $tmpFile