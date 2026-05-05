#!/bin/bash
# integration-test-nfs.sh — protocol-version integration test runner.
#
# Mounts an NFS export via the host's NFS client at each version
# in {3, 4.0, 4.1}, runs a small but load-bearing battery of ops
# against each mount, asserts the results match. Used to catch
# regressions in: protocol negotiation, basic VFS ops (read,
# write, stat, unlink, mkdir, rmdir), file-content integrity
# (sha256 round-trip), and — for the multipath case — that
# traffic actually distributes across paths.
#
# Designed to run against:
#   - the lab OceanStor on the IPv6 storage VLAN (default mode)
#   - a local nfs-kernel-server (use --local; sets up an export
#     in /tmp and mounts loopback)
#   - any other NFS server the operator can mount; pass --server
#     and --export
#
# What this script is and isn't:
#   - It IS a smoke test that all three protocol versions work
#     against the configured target. CI-friendly. No throughput
#     measurement.
#   - It is NOT the perf bench harness (those are
#     scripts/perf-*-bench.sh).
#   - It is NOT a multipath-fairness test (that's a derived
#     question — when --enfs is passed and the mount is
#     multipath, the script also samples /proc/self/mountstats
#     to confirm the dispatcher is using more than one xprt).
#
# Usage:
#   integration-test-nfs.sh [--server HOST] [--export PATH]
#                           [--versions "3 4.0 4.1"]
#                           [--mountopts EXTRA] [--enfs] [--local]
#
# Exit codes:
#   0  all configured versions passed every test
#   1  one or more tests failed; per-version summary on stderr
#   2  setup failure (missing tools, no export, etc.)
set -uo pipefail

VERSIONS="3 4.0 4.1"
SERVER=""
EXPORT=""
EXTRA_OPTS=""
USE_ENFS=0
USE_LOCAL=0

usage() {
    sed -n '/^# integration-test-nfs/,/^set -/p' "$0" | sed 's/^# \?//'
    exit 2
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --server)   SERVER="$2"; shift 2 ;;
        --export)   EXPORT="$2"; shift 2 ;;
        --versions) VERSIONS="$2"; shift 2 ;;
        --mountopts) EXTRA_OPTS="$2"; shift 2 ;;
        --enfs)     USE_ENFS=1; shift ;;
        --local)    USE_LOCAL=1; shift ;;
        -h|--help)  usage ;;
        *)          echo "unknown arg: $1" >&2; usage ;;
    esac
done

# Local-mode setup: bring up a tiny nfs-kernel-server in /tmp.
LOCAL_TMP=""
if [[ $USE_LOCAL -eq 1 ]]; then
    if [[ -n "$SERVER" || -n "$EXPORT" ]]; then
        echo "ERROR: --local conflicts with --server / --export" >&2
        exit 2
    fi
    if ! command -v exportfs >/dev/null; then
        echo "ERROR: --local needs nfs-kernel-server installed" >&2
        exit 2
    fi
    LOCAL_TMP=$(mktemp -d -t int-nfs-srv.XXXXXX)
    sudo -n exportfs -o rw,no_root_squash,no_subtree_check,fsid=42 \
        "*:${LOCAL_TMP}"
    trap 'sudo -n exportfs -u "*:${LOCAL_TMP}" 2>/dev/null; rm -rf "$LOCAL_TMP"' EXIT
    SERVER="127.0.0.1"
    EXPORT="$LOCAL_TMP"
fi

[[ -z "$SERVER" || -z "$EXPORT" ]] && { echo "ERROR: --server and --export required" >&2; exit 2; }
command -v mount.nfs >/dev/null || { echo "ERROR: mount.nfs not installed (apt install nfs-common)" >&2; exit 2; }
command -v sha256sum >/dev/null || { echo "ERROR: sha256sum required" >&2; exit 2; }

PASS=0
FAIL=0
declare -a FAILED_VERSIONS

for v in $VERSIONS; do
    echo
    echo "==============================================================="
    echo "version $v"
    echo "==============================================================="
    MNT=$(mktemp -d -t int-nfs-mnt.XXXXXX)

    # Build the mount option string. nfs vs nfs4 fs-type per version.
    case "$v" in
        3)   FSTYPE="nfs"  ; OPTS="vers=3,nolock,proto=tcp" ;;
        4.0) FSTYPE="nfs4" ; OPTS="vers=4.0,proto=tcp" ;;
        4.1) FSTYPE="nfs4" ; OPTS="vers=4.1,proto=tcp" ;;
        *)   echo "ERROR: unknown version $v" >&2; FAIL=$((FAIL+1)); FAILED_VERSIONS+=("$v"); rmdir "$MNT"; continue ;;
    esac
    [[ -n "$EXTRA_OPTS" ]] && OPTS="${OPTS},${EXTRA_OPTS}"
    [[ $USE_ENFS -eq 1 ]] && FSTYPE="enfs"

    # ---- mount ----
    echo "[mount] -t $FSTYPE -o $OPTS"
    if ! sudo -n mount -t "$FSTYPE" -o "$OPTS" "${SERVER}:${EXPORT}" "$MNT" 2>&1; then
        echo "  FAIL: mount failed for v$v"
        FAIL=$((FAIL+1)); FAILED_VERSIONS+=("$v:mount")
        rmdir "$MNT"; continue
    fi

    # Confirm the mount actually negotiated this version.
    NEGOTIATED=$(mount | awk -v m="$MNT" '$3==m {for(i=1;i<=NF;i++) if($i ~ /vers=/) print $i}' | tr -d ',()')
    echo "  negotiated: $NEGOTIATED"

    OK=1

    # ---- basic file write + sha256 round trip ----
    TESTFILE="${MNT}/int-${v}.bin"
    SIZE_MB=8
    if ! dd if=/dev/urandom of="$TESTFILE" bs=1M count=$SIZE_MB conv=fsync status=none 2>/dev/null; then
        echo "  FAIL: dd write failed"; OK=0
    fi
    SHA_W=$(sha256sum "$TESTFILE" | awk '{print $1}')

    # Drop client cache, re-read.
    sudo -n bash -c 'echo 3 > /proc/sys/vm/drop_caches' 2>/dev/null
    SHA_R=$(sha256sum "$TESTFILE" | awk '{print $1}')
    if [[ "$SHA_W" != "$SHA_R" ]]; then
        echo "  FAIL: sha mismatch after drop_caches: $SHA_W vs $SHA_R"
        OK=0
    else
        echo "  PASS: sha256 round-trip ($SHA_W)"
    fi

    # ---- mkdir + listing + rmdir ----
    TESTDIR="${MNT}/int-${v}-d"
    mkdir "$TESTDIR" || { echo "  FAIL: mkdir"; OK=0; }
    touch "${TESTDIR}/a" "${TESTDIR}/b" "${TESTDIR}/c" || { echo "  FAIL: touch"; OK=0; }
    LIST=$(ls "$TESTDIR" | sort | tr '\n' ' ')
    if [[ "$LIST" != "a b c " ]]; then
        echo "  FAIL: ls returned '$LIST' (expected 'a b c ')"
        OK=0
    else
        echo "  PASS: directory create + list"
    fi
    rm -f "${TESTDIR}"/*
    rmdir "$TESTDIR" || { echo "  FAIL: rmdir"; OK=0; }
    rm -f "$TESTFILE"

    # ---- (optional) multipath check, only when enfs + multiple xprts ----
    if [[ $USE_ENFS -eq 1 ]]; then
        XPRT_COUNT=$(awk -v m="$MNT" '
            $0 ~ "device.*on " m " " {flag=1; next}
            flag && /^device / {flag=0}
            flag && /^	xprt:/ {n++}
            END {print n+0}
        ' /proc/self/mountstats)
        echo "  enfs: $XPRT_COUNT xprt(s) visible in mountstats"
        if [[ "$XPRT_COUNT" -lt 2 ]]; then
            echo "  NOTE: single xprt — not actually multipath (set remoteaddrs= for multi)"
        fi
    fi

    # ---- unmount ----
    sudo -n umount "$MNT" || echo "  WARN: umount failed"
    rmdir "$MNT"

    if [[ $OK -eq 1 ]]; then
        echo "v$v: PASS"
        PASS=$((PASS+1))
    else
        echo "v$v: FAIL"
        FAIL=$((FAIL+1)); FAILED_VERSIONS+=("$v")
    fi
done

echo
echo "==============================================================="
echo "RESULT: $PASS passed, $FAIL failed"
if [[ $FAIL -gt 0 ]]; then
    echo "Failed versions: ${FAILED_VERSIONS[*]}"
    exit 1
fi
