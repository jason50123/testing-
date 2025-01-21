#!/bin/bash

# copy $SZ bytes from $SRC to $DST

MY_NAME=${MY_NAME:-$(basename $(realpath $0) .sh)}
DEBUGFS=${DEBUGFS:-/sbin/debugfs}
TMPFILE=${TMPFILE:-/tmp/${MY_NAME}-cmd.tmp}
TMPDATA="$TMPFILE.dat"

DISK_FILE=$1
SRC=$2
DST=$3
SZ=$4
REPEATS=${5:-1}
TAIL_PATTERN=${6:-""}

if [[ $# -lt 4 ]]; then
    echo "Usage: $0 DISK_FILE SRC_FILE DST_FILE BYTES [REPEATS] [TAIL_PATTERN]"
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

# copy first $SZ bytes from $SRC to TMPDATA
plen=${#TAIL_PATTERN}

for i in $(seq 1 $REPEATS); do
  head -c $(( ${SZ} - ${plen} )) ${SRC} > "${TMPDATA}.${i}"
  echo -ne "$TAIL_PATTERN" >> "${TMPDATA}.${i}"
  md5sum "${TMPDATA}.${i}"
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