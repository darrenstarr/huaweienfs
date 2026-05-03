#!/usr/bin/env bash
# fetch-vendor-ubuntu.sh - lazily materialise vendor/<target>/ from the
# upstream Ubuntu kernel source package.
#
# Usage:
#   scripts/fetch-vendor-ubuntu.sh ubuntu-6.8
#   scripts/fetch-vendor-ubuntu.sh ubuntu-6.14
#   scripts/fetch-vendor-ubuntu.sh ubuntu-7.0
#
# Inputs (committed to git):
#   vendor/<target>/UPSTREAM-REVISION   shell-sourceable; must set
#                                       SRC_PKG, SRC_VERSION, ARCHIVE_URL.
#   vendor/<target>/MANIFEST            list of paths (files or dirs) to
#                                       copy from the extracted source
#                                       tree into vendor/<target>/.
#
# Output:
#   vendor/<target>/                    materialised — gitignored except
#                                       for UPSTREAM-REVISION + MANIFEST.
#
# Cache:
#   $ENFS_VENDOR_CACHE (defaults to $HOME/.cache/enfs-vendor)
#   keyed by ${SRC_PKG}_${SRC_VERSION}, so subsequent runs are no-ops
#   when the pin hasn't moved.
#
# Network: hits ARCHIVE_URL on first run for a given pin. CI caches
# this dir between runs.

set -euo pipefail

TARGET="${1:?usage: $0 <target>  (e.g. ubuntu-6.8)}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR_DIR="$ROOT/vendor/$TARGET"
PIN_FILE="$VENDOR_DIR/UPSTREAM-REVISION"
MANIFEST="$VENDOR_DIR/MANIFEST"
CACHE_ROOT="${ENFS_VENDOR_CACHE:-$HOME/.cache/enfs-vendor}"

log() { echo "[fetch-vendor:$TARGET] $*" >&2; }
die() { log "ERROR: $*"; exit 1; }

[ -f "$PIN_FILE" ] || die "missing $PIN_FILE — is '$TARGET' a real target?"
[ -f "$MANIFEST" ] || die "missing $MANIFEST"

# Source the pin
unset SRC_PKG SRC_VERSION ARCHIVE_URL
# shellcheck disable=SC1090
. "$PIN_FILE"
[ -n "${SRC_PKG:-}" ]     || die "$PIN_FILE missing SRC_PKG="
[ -n "${SRC_VERSION:-}" ] || die "$PIN_FILE missing SRC_VERSION="
[ -n "${ARCHIVE_URL:-}" ] || die "$PIN_FILE missing ARCHIVE_URL="

CACHE="$CACHE_ROOT/${SRC_PKG}_${SRC_VERSION}"
DSC_URL="${ARCHIVE_URL%/}/${SRC_PKG}_${SRC_VERSION}.dsc"

# Decide whether to re-materialise. Skip if the vendor dir already
# matches this pin (we stamp it with the pin's SRC_VERSION).
STAMP="$VENDOR_DIR/.pin-stamp"
WANT_STAMP="${SRC_PKG}_${SRC_VERSION}"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT_STAMP" ]; then
    # Verify a couple of MANIFEST entries actually exist so a corrupt
    # tree isn't silently accepted.
    sample=$(grep -v -E '^#|^$' "$MANIFEST" | head -1)
    if [ -e "$VENDOR_DIR/$sample" ]; then
        log "vendor/$TARGET already at $WANT_STAMP — skipping"
        exit 0
    fi
fi

# Fetch + extract into the cache (idempotent).
mkdir -p "$CACHE"
if [ ! -f "$CACHE/.extracted" ]; then
    log "downloading $SRC_PKG $SRC_VERSION via dget"
    command -v dget >/dev/null 2>&1 || \
        die "dget not found — install the 'devscripts' package"
    ( cd "$CACHE" && dget -d -u "$DSC_URL" )
    log "extracting $SRC_PKG $SRC_VERSION via dpkg-source -x"
    command -v dpkg-source >/dev/null 2>&1 || \
        die "dpkg-source not found — install the 'dpkg-dev' package"
    ( cd "$CACHE" && dpkg-source -x --no-check "${SRC_PKG}_${SRC_VERSION}.dsc" )
    touch "$CACHE/.extracted"
fi

# Find the extracted dir. dpkg-source picks <package>-<upstream-version>/
SRC_TREE=$(find "$CACHE" -maxdepth 1 -type d -name "${SRC_PKG}-*" | head -1)
[ -d "$SRC_TREE" ] || die "extraction failed (no ${SRC_PKG}-* dir under $CACHE)"
log "using source tree: $SRC_TREE"

# Materialise vendor/$TARGET/ from MANIFEST. Build into a sibling dir
# then atomically swap, so a half-finished run doesn't leave the
# repo in a broken state.
TMP="$VENDOR_DIR.fetching.$$"
trap 'rm -rf "$TMP"' EXIT
rm -rf "$TMP"
mkdir -p "$TMP"
cp "$PIN_FILE" "$TMP/UPSTREAM-REVISION"
cp "$MANIFEST" "$TMP/MANIFEST"

missing=0
copied=0
while IFS= read -r entry; do
    case "$entry" in ''|\#*) continue ;; esac
    src="$SRC_TREE/$entry"
    dst="$TMP/$entry"
    if [ -d "$src" ]; then
        mkdir -p "$(dirname "$dst")"
        cp -a "$src" "$dst"
        copied=$((copied + 1))
    elif [ -f "$src" ]; then
        mkdir -p "$(dirname "$dst")"
        cp "$src" "$dst"
        copied=$((copied + 1))
    else
        log "  WARN: $entry not in upstream (kernel-version drift?)"
        missing=$((missing + 1))
    fi
done < "$MANIFEST"

# Atomic swap.
rm -rf "$VENDOR_DIR"
mv "$TMP" "$VENDOR_DIR"
trap - EXIT
echo "$WANT_STAMP" > "$VENDOR_DIR/.pin-stamp"

log "materialised vendor/$TARGET: $copied entries copied, $missing missing"
log "$(find "$VENDOR_DIR" -type f | wc -l) total files"
