#!/usr/bin/env bash
# apply-compat-shims.sh — backwards-compatibility wrapper.
#
# The pre-Option-B′ porting script lived here. The actual porting logic
# now lives in scripts/build-src-tree.sh, which materialises src/ from:
#   - vendor/ubuntu-7.0/                (stock Ubuntu kernel files)
#   - patches/ubuntu-7.0/series         (focused patches on Ubuntu)
#   - vendor/openeuler/{enfs/,*_adapter.*}  (OE-only "new" files)
#   - compat/                           (kernel-version compat shims)
#
# This wrapper is kept so anything (docs, CI snippets, muscle memory)
# that still says `scripts/apply-compat-shims.sh` keeps working with no
# arguments and uses the project's default layout.
#
# New callers should invoke scripts/build-src-tree.sh directly.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

UBUNTU_VENDOR_DIR="${UBUNTU_VENDOR_DIR:-$ROOT_DIR/vendor/ubuntu-7.0}"
OE_VENDOR_DIR="${OE_VENDOR_DIR:-$ROOT_DIR/vendor/openeuler}"
PATCHES_DIR="${PATCHES_DIR:-$ROOT_DIR/patches/ubuntu-7.0}"
COMPAT_DIR="${COMPAT_DIR:-$ROOT_DIR/compat}"
SRC_DIR="${SRC_DIR:-$ROOT_DIR/src}"

echo "[apply-compat-shims] (compat wrapper) -> build-src-tree.sh"
exec "$SCRIPT_DIR/build-src-tree.sh" \
    "$UBUNTU_VENDOR_DIR" \
    "$OE_VENDOR_DIR" \
    "$PATCHES_DIR" \
    "$COMPAT_DIR" \
    "$SRC_DIR"
