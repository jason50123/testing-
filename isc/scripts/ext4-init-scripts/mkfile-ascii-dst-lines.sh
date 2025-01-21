#!/bin/bash

# generate $LINES bytes ascii text to $DST

MY_NAME=${MY_NAME:-$(basename $(realpath $0) .sh)}
DEBUGFS=${DEBUGFS:-/sbin/debugfs}
TMPFILE=${TMPFILE:-/tmp/${MY_NAME}-cmd.tmp}
TMPDATA="$TMPFILE.dat"

DISK_FILE=$1
DST=$2
NR_LINES=$3
REPEATS=${4:-1}

if [[ $# -le 3 ]]; then
    echo "Usage: $0 DISK_FILE DST_FILE NR_LINES [REPEATS]"
    exit -1;
fi

if [[ ! -e "${DISK_FILE}" ]]; then
    echo "disk file '${DISK_FILE}' not exists"
    exit -1;
fi

# clear tmp file
echo -n "" > ${TMPFILE}

# generate parent dirs
DST_NAME="$(basename ${DST})"
DST_DIR=""
for d in $(dirname "$DST" | tr '/' ' '); do
  DST_DIR+="/${d}"
  echo "mkdir ${DST_DIR}" >> ${TMPFILE}
done

# generate $NR_LINES bytes ascii-text
for i in $(seq 1 $REPEATS); do
  tr -dc 'A-Za-z \n' < /dev/urandom | awk '{printf "%d__%s\n", NR, $0}' | head -n $NR_LINES > "${TMPDATA}.${i}"
  wc -lc "${TMPDATA}.${i}"
done

# copy into disk
echo "cd ${DST_DIR}" >> ${TMPFILE}
for i in $(seq 1 $REPEATS); do
  echo "write ${TMPDATA}.${i} ${DST_NAME}.${i}" >> ${TMPFILE}
done
echo "quit" >> ${TMPFILE}

# run script
export PAGER=cat
CMD="$DEBUGFS -w -f ${TMPFILE} ${DISK_FILE}"

echo "$CMD"
$CMD > /dev/null
echo "... done!"
