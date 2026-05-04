#!/usr/bin/env bash
# fetch-vendor-ubuntu.sh — materialise vendor/<target>/ from the
# linux-source-X.Y.Z package installed on this machine.
#
# Usage:
#   scripts/fetch-vendor-ubuntu.sh ubuntu-6.8
#   scripts/fetch-vendor-ubuntu.sh ubuntu-6.14
#   scripts/fetch-vendor-ubuntu.sh ubuntu-7.0
#
# Inputs (committed to git):
#   vendor/<target>/UPSTREAM-REVISION   shell-sourceable; must set
#                                       LINUX_SOURCE_PKG=<deb name>.
#   vendor/<target>/MANIFEST            list of paths (files or dirs) to
#                                       copy from the extracted source
#                                       tree into vendor/<target>/.
#
# Output:
#   vendor/<target>/                    materialised — gitignored except
#                                       for UPSTREAM-REVISION + MANIFEST.
#
# Source of the kernel tree (in priority order):
#   1. $ENFS_LINUX_SOURCE_TREE                    pre-extracted source tree
#                                                 dir; copy directly from it.
#                                                 Use for HWE kernels (no
#                                                 linux-source-X.Y.Z binary
#                                                 deb exists for those —
#                                                 see #16) or any custom
#                                                 setup.
#   2. /usr/src/<LINUX_SOURCE_PKG>.tar.{bz2,xz,gz}
#                                                 provided by Ubuntu's
#                                                 linux-source-X.Y.Z
#                                                 package (apt install).
#                                                 Available for GA kernels.
#
# Cache:
#   $ENFS_VENDOR_CACHE (defaults to $HOME/.cache/enfs-vendor),
#   keyed by ${LINUX_SOURCE_PKG}_<sha-prefix> so subsequent runs are
#   no-ops when the source hasn't changed.
#
# Network: NONE. This script never reaches the internet. If the
# expected linux-source package isn't installed, it errors with a
# clear "apt install ..." hint. Closes #16.

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

# Source the pin. Required field: LINUX_SOURCE_PKG.
unset LINUX_SOURCE_PKG SRC_PKG SRC_VERSION ARCHIVE_URL
# shellcheck disable=SC1090
. "$PIN_FILE"

if [ -z "${LINUX_SOURCE_PKG:-}" ]; then
    if [ -n "${SRC_PKG:-}" ] || [ -n "${ARCHIVE_URL:-}" ]; then
        die "$PIN_FILE uses the legacy SRC_PKG/SRC_VERSION/ARCHIVE_URL format. \
Convert to LINUX_SOURCE_PKG=<deb-name> and remove SRC_PKG/SRC_VERSION/ARCHIVE_URL. \
See vendor/ubuntu-6.8/UPSTREAM-REVISION for the new format."
    fi
    die "$PIN_FILE missing LINUX_SOURCE_PKG="
fi

# Resolve the source tree. Two paths:
#   (a) ENFS_LINUX_SOURCE_TREE points to an already-extracted dir
#       (HWE / custom setup). Use it directly; no extraction.
#   (b) /usr/src/${LINUX_SOURCE_PKG}.tar.* (linux-source-X.Y.Z apt
#       package). Extract to cache.
SRC_TREE=
TARBALL=

if [ -n "${ENFS_LINUX_SOURCE_TREE:-}" ]; then
    SRC_TREE="$ENFS_LINUX_SOURCE_TREE"
    [ -d "$SRC_TREE" ] || die "ENFS_LINUX_SOURCE_TREE=$SRC_TREE is not a directory"
    log "using pre-extracted tree at $SRC_TREE (override)"
    # Cache key derives from the override path so swapping trees
    # invalidates appropriately. Hash the absolute path; trees with
    # the same path are assumed identical.
    SRC_KEY=$(printf '%s' "$SRC_TREE" | sha256sum | cut -c1-16)
    WANT_STAMP="override_${SRC_KEY}"
else
    for ext in bz2 xz gz; do
        candidate="/usr/src/${LINUX_SOURCE_PKG}.tar.${ext}"
        if [ -f "$candidate" ]; then
            TARBALL="$candidate"
            break
        fi
    done

    if [ -z "$TARBALL" ]; then
        die "missing /usr/src/${LINUX_SOURCE_PKG}.tar.{bz2,xz,gz}
       Install with:  sudo apt install ${LINUX_SOURCE_PKG}
       (the package ships the tarball under /usr/src/; nothing else needed.)
       OR set ENFS_LINUX_SOURCE_TREE=/path/to/extracted/linux-X.Y.Z to
       point at an already-extracted source tree (HWE workflow — see
       docs/internals/10-testing-and-ci.md §10.4)."
    fi
    log "using $TARBALL"
    # Cache key: package name + first 16 hex chars of tarball SHA. Reusing
    # the same package version → cache hit. New package version pushed by
    # Ubuntu → new tarball SHA → cache miss + re-extract.
    TARBALL_SHA=$(sha256sum "$TARBALL" | cut -c1-16)
    CACHE="$CACHE_ROOT/${LINUX_SOURCE_PKG}_${TARBALL_SHA}"
    WANT_STAMP="${LINUX_SOURCE_PKG}_${TARBALL_SHA}"
fi

# Skip the whole pipeline if vendor dir is already at this stamp AND
# at least one MANIFEST entry exists (catches a half-deleted tree).
STAMP="$VENDOR_DIR/.pin-stamp"
if [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$WANT_STAMP" ]; then
    sample=$(grep -v -E '^#|^$' "$MANIFEST" | head -1)
    if [ -n "$sample" ] && [ -e "$VENDOR_DIR/$sample" ]; then
        log "vendor/$TARGET already at $WANT_STAMP — skipping"
        exit 0
    fi
fi

# Extract into the cache (idempotent — keyed on tarball SHA).
# Skipped entirely when ENFS_LINUX_SOURCE_TREE was set (SRC_TREE
# already points to the override).
if [ -z "${ENFS_LINUX_SOURCE_TREE:-}" ]; then
    mkdir -p "$CACHE"
    if [ ! -f "$CACHE/.extracted" ]; then
        log "extracting $TARBALL into cache (this can take ~30 s)"
        case "$TARBALL" in
            *.tar.bz2) tar -C "$CACHE" -xjf "$TARBALL" ;;
            *.tar.xz)  tar -C "$CACHE" -xJf "$TARBALL" ;;
            *.tar.gz)  tar -C "$CACHE" -xzf "$TARBALL" ;;
            *)         die "unrecognised tarball extension: $TARBALL" ;;
        esac
        touch "$CACHE/.extracted"
    else
        log "cache hit: $CACHE"
    fi

    # Find the extracted top-level dir. Tarballs usually have a single
    # top-level directory named like the tarball (linux-source-X.Y.Z/),
    # but be tolerant of other prefixes.
    SRC_TREE=$(find "$CACHE" -maxdepth 1 -mindepth 1 -type d | head -1)
    [ -d "$SRC_TREE" ] || die "no extracted directory under $CACHE — corrupt tarball?"
fi

# Materialise vendor/$TARGET/ from MANIFEST. Build into a sibling dir
# then atomically swap, so a half-finished run doesn't leave the repo
# in a broken state.
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
