#!/bin/bash
# Per-test fio runner: one block size at a time, fresh JSON per test.
# Args: PERFDIR LABEL OUTDIR
set -u
PERFDIR=${1:?perfdir}
LABEL=${2:?label}
OUTDIR=${3:?outdir}
mkdir -p "$OUTDIR"
SIZES="8k 16k 32k 64k 128k 256k 512k 1m 2m"

for op in write read; do
  for bs in $SIZES; do
    name="${LABEL}_${op}_${bs}"
    file="${PERFDIR}/wrk_${bs}_${LABEL}"
    out="${OUTDIR}/${name}.json"
    echo ">>> $name"
    fio --name="$name" --rw=$op --bs=$bs \
        --filename="$file" \
        --direct=1 --ioengine=psync --numjobs=1 \
        --size=4G --runtime=20 --ramp_time=1 --time_based=1 \
        --output="$out" --output-format=json \
        --status-interval=10 \
        2>&1 | tail -2
  done
done
echo "DONE"
