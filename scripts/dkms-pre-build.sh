#!/usr/bin/env bash
# dkms-pre-build.sh — DKMS PRE_BUILD hook.
#
# DKMS sets these env vars before invoking us:
#   $kernelver           kernel version we're building for
#   $dkms_tree           usually /var/lib/dkms
#   We are exec'd from   $dkms_tree/$PACKAGE_NAME/$PACKAGE_VERSION/build
#
# Our job: turn the staged source tree into a buildable src/ by running
# scripts/build-src-tree.sh against the vendored OE + Ubuntu sources
# and the patches/ directory we shipped.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

UBUNTU_VENDOR_DIR="$PWD/vendor/ubuntu-7.0"
OE_VENDOR_DIR="$PWD/vendor/openeuler"
PATCHES_DIR="$PWD/patches/ubuntu-7.0"
COMPAT_DIR="$PWD/compat"
SRC_DIR="$PWD/src"

if [[ ! -x scripts/build-src-tree.sh ]]; then
    echo "[dkms-pre-build] missing scripts/build-src-tree.sh in $PWD" >&2
    exit 1
fi

scripts/build-src-tree.sh \
    "$UBUNTU_VENDOR_DIR" \
    "$OE_VENDOR_DIR" \
    "$PATCHES_DIR" \
    "$COMPAT_DIR" \
    "$SRC_DIR"
