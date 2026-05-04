#!/usr/bin/env bash
# fetch-linux-headers.sh — lazily download the upstream Linux headers
# listed in tests/LINUX-HEADERS-MANIFEST into tests/build/linux-headers/.
#
# Pin source: tests/UPSTREAM-REVISION (sourced as shell; sets
# LINUX_TAG and FETCH_BASE).
#
# Cache: $ENFS_TESTS_HEADER_CACHE (defaults to
# $HOME/.cache/enfs-tests-headers/<LINUX_TAG>). Subsequent runs are
# no-ops when the pin hasn't moved, so this is safe to run on every
# `make test`.
#
# This script is intentionally small: no curl/wget heroics, no parallel
# jobs, no retry loop. If a fetch fails, the failure is loud and the
# user can re-run. We're downloading ~6 small files; speed is not the
# concern, reproducibility is.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
PIN_FILE="$ROOT/UPSTREAM-REVISION"
MANIFEST="$ROOT/LINUX-HEADERS-MANIFEST"
OUT_DIR="$ROOT/build/linux-headers"

log() { echo "[fetch-linux-headers] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

[ -f "$PIN_FILE" ] || die "missing $PIN_FILE"
[ -f "$MANIFEST" ] || die "missing $MANIFEST"

unset LINUX_TAG FETCH_BASE
# shellcheck disable=SC1090
. "$PIN_FILE"
[ -n "${LINUX_TAG:-}" ]  || die "$PIN_FILE missing LINUX_TAG="
[ -n "${FETCH_BASE:-}" ] || die "$PIN_FILE missing FETCH_BASE="

CACHE_ROOT="${ENFS_TESTS_HEADER_CACHE:-$HOME/.cache/enfs-tests-headers}"
CACHE_DIR="$CACHE_ROOT/$LINUX_TAG"
STAMP="$CACHE_DIR/.fetched"

# Pick a downloader.
if command -v curl >/dev/null 2>&1; then
    fetch() { curl -fsSL "$1" -o "$2"; }
elif command -v wget >/dev/null 2>&1; then
    fetch() { wget -q -O "$2" "$1"; }
else
    die "neither curl nor wget is installed"
fi

if [ -f "$STAMP" ]; then
    log "cache hit for $LINUX_TAG ($CACHE_DIR) — copying to $OUT_DIR"
else
    log "fetching upstream Linux headers @ $LINUX_TAG into cache"
    mkdir -p "$CACHE_DIR"
    while IFS= read -r line; do
        # strip comments and blank lines
        path="${line%%#*}"
        path="${path## }"; path="${path%% }"
        [ -z "$path" ] && continue
        url="$FETCH_BASE/$path"
        dst="$CACHE_DIR/$path"
        mkdir -p "$(dirname "$dst")"
        log "  $path"
        fetch "$url" "$dst" || die "failed to fetch $url"
    done < "$MANIFEST"
    touch "$STAMP"
fi

# Mirror cache → output dir (rsync if available, else cp -a).
mkdir -p "$OUT_DIR"
if command -v rsync >/dev/null 2>&1; then
    rsync -a --delete --exclude='.fetched' "$CACHE_DIR/" "$OUT_DIR/"
else
    rm -rf "$OUT_DIR"
    mkdir -p "$OUT_DIR"
    (cd "$CACHE_DIR" && find . -type f ! -name '.fetched' -print0 | \
        xargs -0 -I{} bash -c 'mkdir -p "$0/$(dirname "$1")" && cp "$1" "$0/$1"' "$OUT_DIR" "{}")
fi

log "done: $(find "$OUT_DIR" -type f | wc -l) headers under $OUT_DIR"
