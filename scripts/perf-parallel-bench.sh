#!/bin/bash
# Parallel-stream test runner.
# Args: PERFDIR LABEL OUTDIR
set -u
PERFDIR=${1:?perfdir}
LABEL=${2:?label}
OUTDIR=${3:?outdir}
mkdir -p "$OUTDIR"

# Per-stream file size kept small so 64 streams × 2 bs only consumes
# ~256 GB peak. Long enough that runtime caps before file fills.
SIZE=2G
RUNTIME=20

for bs in 1m 2m; do
  # WRITE phase first so the READ phase has files to read.
  for n in 1 4 16 64; do
    name="${LABEL}_W_${bs}_n${n}"
    out="${OUTDIR}/${name}.json"
    sub="${PERFDIR}/par_${bs}_${LABEL}_n${n}"
    mkdir -p "$sub"
    echo ">>> $name"
    fio --name="$name" --rw=write --bs=$bs \
        --directory="$sub" \
        --direct=1 --ioengine=psync --numjobs=$n \
        --size=$SIZE --runtime=$RUNTIME --ramp_time=2 --time_based=1 \
        --group_reporting=1 \
        --output="$out" --output-format=json \
        2>&1 | tail -2
  done
  for n in 1 4 16 64; do
    name="${LABEL}_R_${bs}_n${n}"
    out="${OUTDIR}/${name}.json"
    sub="${PERFDIR}/par_${bs}_${LABEL}_n${n}"
    echo ">>> $name"
    fio --name="$name" --rw=read --bs=$bs \
        --directory="$sub" \
        --direct=1 --ioengine=psync --numjobs=$n \
        --size=$SIZE --runtime=$RUNTIME --ramp_time=2 --time_based=1 \
        --group_reporting=1 \
        --output="$out" --output-format=json \
        2>&1 | tail -2
  done
done
echo "DONE"
