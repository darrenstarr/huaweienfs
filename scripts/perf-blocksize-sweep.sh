#!/bin/bash
# Single-stream block-size sweep for finding the optimal bs for an
# enfs mount. Sweeps 4K through 1M (2M dropped — known inefficient
# per the perf-tuning chapter). Mirrors scripts/perf-seq-bench.sh
# conventions for output naming so the existing reporters work.
#
# Args:
#   PERFDIR   absolute path inside the writer mount
#   LABEL     tag included in output JSON name (e.g. v3rr_bsweep)
#   OUTDIR    directory for fio JSON output
#
# fio invocation: psync, direct=1, numjobs=1, runtime=30 + ramp=2,
# 4 GiB per-block-size file (much larger than reasonable client cache),
# timeout-wrapped at 90 s for issue #27 protection.
#
# Output: ${LABEL}_${op}_${bs}_n1.json per test.
set -uo pipefail

PERFDIR="${1:?usage: $0 PERFDIR LABEL OUTDIR}"
LABEL="${2:?label}"
OUTDIR="${3:?outdir}"

if [[ ! -d "$PERFDIR" ]]; then
    echo "ERROR: $PERFDIR is not a directory" >&2
    exit 2
fi
mkdir -p "$OUTDIR"

SIZES="4k 8k 16k 32k 64k 128k 256k 512k 1m"
SIZE=4G
RUNTIME=30
RAMP=2
TIMEOUT=90

for op in write read; do
    for bs in $SIZES; do
        name="${LABEL}_${op}_${bs}_n1"
        file="${PERFDIR}/wrk_${bs}_${LABEL}"
        out="${OUTDIR}/${name}.json"
        echo ">>> $name"
        timeout --kill-after=5 "$TIMEOUT" \
        fio --name="$name" --rw=$op --bs=$bs \
            --filename="$file" \
            --direct=1 --ioengine=psync --numjobs=1 \
            --size=$SIZE --runtime=$RUNTIME --ramp_time=$RAMP \
            --time_based=1 \
            --output="$out" --output-format=json \
            --status-interval=10 \
            2>&1 | tail -2 \
            || echo "  (timed out — likely #27 hang; treat as n/a)"
    done
done
echo "DONE — JSONs in $OUTDIR"
