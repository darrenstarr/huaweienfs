#!/bin/bash
# Parallel-stream bench at a single block size, sweeping the worker
# count. Used to characterise scaling at the bs chosen by the
# block-size sweep (perf-blocksize-sweep.sh). Mirrors
# scripts/perf-parallel-bench.sh conventions.
#
# Args:
#   PERFDIR   absolute path inside the writer mount
#   LABEL     tag included in output JSON name (e.g. v3rr_par)
#   OUTDIR    directory for fio JSON output
#   OPT_BS    block size to use (e.g. 1m)
#
# Sweeps numjobs in {1,2,4,8,16,32,64}. Both write and read with the
# same --name= so the read phase reuses files written by the write
# phase. Timeout-wrap 90 s for issue #27 protection.
#
# Output: ${LABEL}_${op}_${OPT_BS}_n${N}.json per test.
set -uo pipefail

PERFDIR="${1:?usage: $0 PERFDIR LABEL OUTDIR OPT_BS}"
LABEL="${2:?label}"
OUTDIR="${3:?outdir}"
OPT_BS="${4:?opt_bs, e.g. 1m}"

if [[ ! -d "$PERFDIR" ]]; then
    echo "ERROR: $PERFDIR is not a directory" >&2
    exit 2
fi
mkdir -p "$OUTDIR"

NS="1 2 4 8 16 32 64"
SIZE=2G
RUNTIME=30
RAMP=2
TIMEOUT=90

for op in write read; do
    for n in $NS; do
        name="${LABEL}_${op}_${OPT_BS}_n${n}"
        sub="${PERFDIR}/par_${OPT_BS}_${LABEL}_n${n}"
        mkdir -p "$sub"
        out="${OUTDIR}/${name}.json"
        echo ">>> $name"
        timeout --kill-after=5 "$TIMEOUT" \
        fio --name="${LABEL}_${OPT_BS}_n${n}" --rw=$op --bs=$OPT_BS \
            --directory="$sub" \
            --direct=1 --ioengine=psync --numjobs=$n \
            --size=$SIZE --runtime=$RUNTIME --ramp_time=$RAMP \
            --time_based=1 \
            --group_reporting=1 \
            --output="$out" --output-format=json \
            2>&1 | tail -2 \
            || echo "  (timed out — likely #27 hang; treat as n/a)"
    done
done
echo "DONE — JSONs in $OUTDIR"
