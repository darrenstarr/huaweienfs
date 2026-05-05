#!/bin/bash
# Quick OceanStor health probe — runs an 8 s direct-write fio against
# a mounted NFS perftest dir and emits GREEN/YELLOW/RED based on
# observed bandwidth. Used to gate longer benchmarks against the
# storage wobble first seen in issue #34.
#
# Args: PERFDIR [LABEL]
#   PERFDIR   absolute path inside the NFS mount (must be a writable
#             directory).
#   LABEL     optional, used in the JSON name (default "health").
#
# Exit codes:
#   0  GREEN   bw >= 100 MB/s
#   1  YELLOW  50 MB/s <= bw < 100 MB/s   (caller may re-try / warn)
#   2  RED     bw <  50 MB/s              (storage wobble — abort)
#   3  fio failed to run / parse error
set -uo pipefail

PERFDIR="${1:?usage: $0 PERFDIR [LABEL]}"
LABEL="${2:-health}"

if [[ ! -d "$PERFDIR" ]]; then
    echo "ERROR: $PERFDIR is not a directory" >&2
    exit 3
fi
if ! command -v jq >/dev/null 2>&1; then
    echo "ERROR: jq is required" >&2
    exit 3
fi

OUT="/tmp/perf-health-${LABEL}-$$.json"
trap 'rm -f "$OUT"' EXIT

echo "=== storage health probe: $PERFDIR ==="
fio --name="health_${LABEL}" --rw=write --bs=1m \
    --directory="$PERFDIR" \
    --direct=1 --ioengine=psync --numjobs=1 \
    --size=512M --runtime=8 --ramp_time=2 --time_based=1 \
    --output="$OUT" --output-format=json \
    >/dev/null 2>&1 || {
    echo "RED: fio failed to run" >&2
    exit 3
}

BW_BYTES=$(jq -r '.jobs[0].write.bw_bytes // 0' "$OUT")
BW_MBS=$(( BW_BYTES / 1000000 ))
echo "observed write bandwidth: ${BW_MBS} MB/s (raw bytes/s: ${BW_BYTES})"

if (( BW_MBS >= 100 )); then
    echo "GREEN — storage healthy"
    exit 0
elif (( BW_MBS >= 50 )); then
    echo "YELLOW — storage degraded but functional; consider re-checking"
    exit 1
else
    echo "RED — storage wobble suspected (issue #34); abort longer benches"
    exit 2
fi
