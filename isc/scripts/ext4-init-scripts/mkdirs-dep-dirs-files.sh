#!/bin/bash

# make $L-level dir tree, and the last level has $N subdirs

MY_NAME=${MY_NAME:-$(basename $(realpath $0) .sh)}
DEBUGFS=${DEBUGFS:-/sbin/debugfs}
TMPFILE=${TMPFILE:-/tmp/${MY_NAME}-cmd.tmp}

DISK_FILE=$1
DEPTH=$(($2 - 1))
DIRS=$3
FILES=${4:-0}

if [[ ! -e "${DISK_FILE}" ]]; then
    echo "disk file '${DISK_FILE}' not exists"
    exit -1;
fi

# generate command script
path="${MY_NAME}"
echo "mkdir ${path}" > ${TMPFILE}

for dep in $(seq 1 ${DEPTH}); do
  path+="/${dep}"
  echo "mkdir ${path}" >> ${TMPFILE}
done

for dir in $(seq 1 ${DIRS}); do
  echo "mkdir ${path}/${dir}" >> ${TMPFILE}
  for file in $(seq 1 ${FILES}); do
    echo "mkdir ${path}/${dir}/${file}" >> ${TMPFILE}
  done
done

echo "quit" >> ${TMPFILE}

# run script
export PAGER=cat
CMD="$DEBUGFS -w -f ${TMPFILE} ${DISK_FILE}"

echo "$CMD"
$CMD > /dev/null
echo "... done!"