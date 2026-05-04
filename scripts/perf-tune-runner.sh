#!/bin/bash
# Tuning experiment harness. Applies a settings group, remounts the
# enfs target, runs the parallel-bench matrix, saves results.
#
# Configuration via env vars (set in secrets/local-env.sh, then
# `source secrets/local-env.sh` before running):
#   ENFS_MOUNT_SRC    e.g. "[2001:db8:2::11]:/dCache"
#   ENFS_MOUNT_TGT    e.g. /mnt
#   ENFS_MOUNT_OPTS   the mount option string (the bench will mutate
#                     rsize=/wsize= per the LABEL, but everything else
#                     comes from this exactly)
#   ENFS_PERFDIR      pre-existing perftest directory inside the mount
#                     (re-used across tier runs to avoid re-writing
#                     hundreds of GB of test files for each iteration)
#
# Args: TUNING_LABEL [--reset]
#   See docs/internals/12-perf-tuning.md §12.3 for label conventions.
set -uo pipefail

LABEL="${1:?tuning label}"
RESET="${2:-}"

: "${ENFS_MOUNT_SRC:?set ENFS_MOUNT_SRC, e.g. via secrets/local-env.sh}"
: "${ENFS_MOUNT_TGT:?set ENFS_MOUNT_TGT}"
: "${ENFS_MOUNT_OPTS:?set ENFS_MOUNT_OPTS}"
: "${ENFS_PERFDIR:?set ENFS_PERFDIR}"

OUT=/tmp/perf-${LABEL}
# Clean previous results so a partial last run doesn't poison the
# new one. Per-tier runs each have a fresh OUT dir.
rm -rf "$OUT"
mkdir -p "$OUT"

if [[ "$RESET" == "--reset" ]]; then
    echo "=== reset to baseline tunables ==="
    sudo bash -c 'echo 2 > /sys/module/sunrpc/parameters/tcp_slot_table_entries' || true
    sudo sysctl -w net.core.rmem_max=4194304
    sudo sysctl -w net.core.wmem_max=4194304
    sudo sysctl -w net.ipv4.tcp_rmem='4096 131072 33554432'
    sudo sysctl -w net.ipv4.tcp_wmem='4096 16384 4194304'
fi

# Apply per-label settings BEFORE remount (sunrpc reads the slot table
# at xprt creation time, so it must be set before the new mount).
case "$LABEL" in
    *slot64*)  sudo bash -c 'echo 64  > /sys/module/sunrpc/parameters/tcp_slot_table_entries' ;;
    *slot128*) sudo bash -c 'echo 128 > /sys/module/sunrpc/parameters/tcp_slot_table_entries' ;;
    *slot256*) sudo bash -c 'echo 256 > /sys/module/sunrpc/parameters/tcp_slot_table_entries' ;;
esac
case "$LABEL" in
    *buf256m*)
        sudo sysctl -w net.core.rmem_max=268435456
        sudo sysctl -w net.core.wmem_max=268435456
        sudo sysctl -w net.ipv4.tcp_rmem='4096 1048576 268435456'
        sudo sysctl -w net.ipv4.tcp_wmem='4096 1048576 268435456' ;;
esac

# Optionally override rsize/wsize via the LABEL.
MOUNT_OPTS="$ENFS_MOUNT_OPTS"
case "$LABEL" in
    *rs4m*)
        MOUNT_OPTS=$(echo "$MOUNT_OPTS" | sed 's/rsize=[0-9]*/rsize=4194304/; s/wsize=[0-9]*/wsize=4194304/') ;;
    *rs8m*)
        MOUNT_OPTS=$(echo "$MOUNT_OPTS" | sed 's/rsize=[0-9]*/rsize=8388608/; s/wsize=[0-9]*/wsize=8388608/') ;;
esac

echo "=== current tunables ==="
echo "tcp_slot_table_entries: $(cat /sys/module/sunrpc/parameters/tcp_slot_table_entries)"
sysctl net.core.rmem_max net.core.wmem_max net.ipv4.tcp_rmem net.ipv4.tcp_wmem | head -4
echo "MOUNT_OPTS: $MOUNT_OPTS"

echo "=== umount + mount ==="
sudo umount "$ENFS_MOUNT_TGT" || true
sleep 1
sudo mount -t nfs -o "$MOUNT_OPTS" "$ENFS_MOUNT_SRC" "$ENFS_MOUNT_TGT" || {
    echo "mount failed; retrying after 5 s"
    sleep 5
    sudo mount -t nfs -o "$MOUNT_OPTS" "$ENFS_MOUNT_SRC" "$ENFS_MOUNT_TGT"
}
mount | grep "$ENFS_MOUNT_TGT"

echo "=== bench writes ==="
bash "$(dirname "$0")/perf-parallel-bench.sh" "$ENFS_PERFDIR" "$LABEL" "$OUT" --writes-only

echo "=== bench reads ==="
bash "$(dirname "$0")/perf-parallel-bench.sh" "$ENFS_PERFDIR" "$LABEL" "$OUT" --reads-only

echo "=== DONE — results in $OUT ==="
ls "$OUT" | wc -l
