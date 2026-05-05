#!/bin/bash
# Parallel-stream test runner, fixed: write and read phases use the
# SAME --name= so the read phase finds files written by the write
# phase (instead of fio re-creating them, which contaminates timing).
#
# Args: PERFDIR LABEL OUTDIR [--writes-only|--reads-only]
set -u
PERFDIR=${1:?perfdir}
LABEL=${2:?label}
OUTDIR=${3:?outdir}
PHASES=${4:-both}    # --writes-only | --reads-only | (default: both)
mkdir -p "$OUTDIR"

SIZE=2G
RUNTIME=20

run_phase() {
    local op="$1" out_letter="$2"
    for bs in 1m 2m; do
        for n in 1 4 16 64; do
            # IMPORTANT: same fio --name= for write and read so the
            # read phase reuses the files the write phase made.
            local jobname="${LABEL}_${bs}_n${n}"
            local outname="${LABEL}_${out_letter}_${bs}_n${n}"
            local sub="${PERFDIR}/par_${bs}_${LABEL}_n${n}"
            mkdir -p "$sub"
            echo ">>> ${outname}"
            # Timeout-wrap so the issue #27 hangs at 2M reads don't
            # block the whole tier matrix. 60 s = ~2.5x the expected
            # 22 s (1 ramp + 20 run + headroom).
            timeout --kill-after=5 60 \
            fio --name="$jobname" --rw=$op --bs=$bs \
                --directory="$sub" \
                --direct=1 --ioengine=psync --numjobs=$n \
                --size=$SIZE --runtime=$RUNTIME --ramp_time=2 --time_based=1 \
                --group_reporting=1 \
                --output="${OUTDIR}/${outname}.json" --output-format=json \
                2>&1 | tail -2 || echo "  (fio timed out — likely #27 hang; treat as n/a)"
        done
    done
}

case "$PHASES" in
    --writes-only) run_phase write W ;;
    --reads-only)  run_phase read  R ;;
    both)          run_phase write W ; run_phase read R ;;
    *)             echo "unknown phase: $PHASES" >&2 ; exit 2 ;;
esac
echo "DONE"
