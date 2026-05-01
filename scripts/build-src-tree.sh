#!/usr/bin/env bash
# build-src-tree.sh - materialise src/ for Option B′.
#
# Usage: build-src-tree.sh <UBUNTU_VENDOR_DIR> <OE_VENDOR_DIR> \
#                          <PATCHES_DIR> <COMPAT_DIR> <SRC_DIR>
#
# Steps (all idempotent):
#   1. Wipe SRC_DIR and recreate.
#   2. Mirror UBUNTU_VENDOR_DIR/{fs,net,include} into SRC_DIR.
#   3. Apply each patch in PATCHES_DIR/series, in order, with `patch -p1`.
#   4. Drop in the OE-only "new" files (enfs/ subdir + adapter glue).
#   5. Mirror COMPAT_DIR into SRC_DIR/compat/.
#   6. Print one-line summary.

set -euo pipefail

if [[ $# -ne 5 ]]; then
    echo "Usage: $(basename "$0") <UBUNTU_VENDOR_DIR> <OE_VENDOR_DIR> <PATCHES_DIR> <COMPAT_DIR> <SRC_DIR>" >&2
    exit 2
fi

UBUNTU_VENDOR_DIR="$1"
OE_VENDOR_DIR="$2"
PATCHES_DIR="$3"
COMPAT_DIR="$4"
SRC_DIR="$5"

log() { echo "[build-src-tree] $*"; }
err() { echo "[build-src-tree] ERROR: $*" >&2; }

for d in "$UBUNTU_VENDOR_DIR" "$OE_VENDOR_DIR" "$COMPAT_DIR"; do
    if [[ ! -d "$d" ]]; then
        err "required directory missing: $d"
        exit 1
    fi
done

# 1. Wipe SRC_DIR and recreate.
log "wiping $SRC_DIR"
rm -rf "$SRC_DIR"
mkdir -p "$SRC_DIR"

# 2. Mirror Ubuntu vendor tree (fs/, net/, include/).
log "mirroring Ubuntu vendor tree from $UBUNTU_VENDOR_DIR"
for sub in fs net include; do
    if [[ -d "$UBUNTU_VENDOR_DIR/$sub" ]]; then
        cp -a "$UBUNTU_VENDOR_DIR/$sub" "$SRC_DIR/"
    else
        err "expected $UBUNTU_VENDOR_DIR/$sub to exist"
        exit 1
    fi
done

# 3. Apply patches from PATCHES_DIR/series, in order.
patches_applied=0
series_file="$PATCHES_DIR/series"
if [[ ! -f "$series_file" ]]; then
    log "no patch series at $series_file, skipping patch step"
else
    log "applying patches from $series_file"
    while IFS= read -r line || [[ -n "$line" ]]; do
        # strip leading/trailing whitespace
        patch_name="${line#"${line%%[![:space:]]*}"}"
        patch_name="${patch_name%"${patch_name##*[![:space:]]}"}"
        # skip blank + comment lines
        [[ -z "$patch_name" || "$patch_name" == \#* ]] && continue
        patch_path="$PATCHES_DIR/$patch_name"
        if [[ ! -f "$patch_path" ]]; then
            err "patch listed in series but not found: $patch_path"
            exit 1
        fi
        log "  applying $patch_name"
        if ! patch -p1 -d "$SRC_DIR" < "$patch_path"; then
            err "patch failed: $patch_name"
            exit 1
        fi
        patches_applied=$((patches_applied + 1))
    done < "$series_file"
fi

# 4. Drop in OE-only "new" files.
log "dropping in OE-only files from $OE_VENDOR_DIR"

# 4a. enfs/ subdir
oe_enfs_dir="$OE_VENDOR_DIR/fs/nfs/enfs"
if [[ ! -d "$oe_enfs_dir" ]]; then
    err "missing OE enfs dir: $oe_enfs_dir"
    exit 1
fi
mkdir -p "$SRC_DIR/fs/nfs"
cp -a "$oe_enfs_dir" "$SRC_DIR/fs/nfs/"

# 4b. enfs_adapter.{c,h}
for f in enfs_adapter.c enfs_adapter.h; do
    src="$OE_VENDOR_DIR/fs/nfs/$f"
    if [[ ! -f "$src" ]]; then
        err "missing OE file: $src"
        exit 1
    fi
    cp "$src" "$SRC_DIR/fs/nfs/"
done

# 4c. sunrpc_enfs_adapter.c
mkdir -p "$SRC_DIR/net/sunrpc"
src="$OE_VENDOR_DIR/net/sunrpc/sunrpc_enfs_adapter.c"
if [[ ! -f "$src" ]]; then
    err "missing OE file: $src"
    exit 1
fi
cp "$src" "$SRC_DIR/net/sunrpc/"

# 4d. sunrpc_enfs_adapter.h
mkdir -p "$SRC_DIR/include/linux/sunrpc"
src="$OE_VENDOR_DIR/include/linux/sunrpc/sunrpc_enfs_adapter.h"
if [[ ! -f "$src" ]]; then
    err "missing OE file: $src"
    exit 1
fi
cp "$src" "$SRC_DIR/include/linux/sunrpc/"

# 5. Mirror compat/ into SRC_DIR/compat/.
log "mirroring compat headers from $COMPAT_DIR"
mkdir -p "$SRC_DIR/compat"
# `cp -a COMPAT/.` to copy contents (works whether or not COMPAT_DIR has a trailing slash).
if [[ -n "$(ls -A "$COMPAT_DIR" 2>/dev/null)" ]]; then
    cp -a "$COMPAT_DIR"/. "$SRC_DIR/compat/"
fi

# 6. Drop the project's top-level Kbuild into src/ so `make M=src` finds it.
project_root="$(cd "$(dirname "${BASH_SOURCE[0]}")"/.. && pwd)"
if [[ -f "$project_root/Kbuild" ]]; then
    log "installing top-level Kbuild as src/Kbuild"
    cp "$project_root/Kbuild" "$SRC_DIR/Kbuild"
fi

# 7. Summary.
file_count=$(find "$SRC_DIR" -type f | wc -l)
echo "[build-src-tree] ${file_count} files, ${patches_applied} patches applied, ready in ${SRC_DIR}"
