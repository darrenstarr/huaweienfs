#!/usr/bin/env bash
# apply-compat-shims.sh - materialise src/ from vendor/openeuler + patches/
#
# Usage: apply-compat-shims.sh VENDOR_DIR SRC_DIR COMPAT_DIR PATCHES_DIR
#
# This script is the deterministic entry point that turns the vendored
# OpenEuler tree into a buildable out-of-tree module set under src/.
# Steps (all idempotent):
#   1. Mirror vendor/openeuler/{fs/nfs,net/sunrpc,include} into src/.
#   2. Drop the top-level Kbuild + per-subdir Kbuild files into src/.
#   3. Drop compat/enfs_compat.h into src/compat/ and rewrite #includes
#      so vendored .c files pick it up first.
#   4. Apply any quilt-style patches from PATCHES_DIR/series in order.
#
# Re-run after editing vendor/, compat/ or patches/ to refresh src/.

set -euo pipefail

VENDOR_DIR="${1:?vendor dir}"
SRC_DIR="${2:?src dir}"
COMPAT_DIR="${3:?compat dir}"
PATCHES_DIR="${4:?patches dir}"

echo "[port] vendor=$VENDOR_DIR"
echo "[port] src=$SRC_DIR"

# 1. Mirror the vendored tree.
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"
cp -a "$VENDOR_DIR/fs"      "$SRC_DIR/"
cp -a "$VENDOR_DIR/net"     "$SRC_DIR/"
cp -a "$VENDOR_DIR/include" "$SRC_DIR/"

# 2. Drop in compat headers.
mkdir -p "$SRC_DIR/compat"
cp -a "$COMPAT_DIR"/* "$SRC_DIR/compat/"

# 3. Per-subdir Kbuild files.
# fs/nfs/enfs already ships its own Makefile (treat as Kbuild).
# fs/nfs and net/sunrpc need slimmed-down Kbuild files that only build
# the objects we ship as replacements (not all of nfs.ko / sunrpc.ko's
# original object set — those stay in the vendored Makefile).
# These will be added in a follow-up commit; for now just verify layout.
echo "[port] TODO: write src/fs/nfs/Kbuild and src/net/sunrpc/Kbuild"
echo "[port] TODO: arrange for compat/enfs_compat.h to be -included"

# 4. Apply patches if PATCHES_DIR/series exists.
if [[ -f "$PATCHES_DIR/series" ]]; then
    echo "[port] applying patches from $PATCHES_DIR/series"
    pushd "$SRC_DIR" >/dev/null
    while read -r patch; do
        [[ -z "$patch" || "$patch" == \#* ]] && continue
        echo "[port]   $patch"
        patch -p1 < "$PATCHES_DIR/$patch"
    done < "$PATCHES_DIR/series"
    popd >/dev/null
fi

echo "[port] done. src/ tree:"
find "$SRC_DIR" -maxdepth 3 -type d | sort
