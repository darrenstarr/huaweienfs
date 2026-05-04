#!/usr/bin/env bash
# dkms-pre-build.sh — DKMS PRE_BUILD hook.
#
# DKMS sets these env vars before invoking us:
#   $kernelver           kernel version we're building for
#   $dkms_tree           usually /var/lib/dkms
#   We are exec'd from   $dkms_tree/$PACKAGE_NAME/$PACKAGE_VERSION/build
#
# Our job: pick the right vendor/+patches/ pair for $kernelver and
# materialise src/ from them via scripts/build-src-tree.sh.

set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."

KVER="${kernelver:-$(uname -r)}"

# Map kernel major.minor to the matching vendored tree.
# Add new entries here when supporting additional kernels.
case "$KVER" in
    7.0.*)   TARGET=ubuntu-7.0 ;;
    6.14.*)  TARGET=ubuntu-6.14 ;;
    6.11.*)  TARGET=ubuntu-6.11 ;;
    6.8.*)   TARGET=ubuntu-6.8 ;;
    *)
        echo "[dkms-pre-build] ERROR: kernel $KVER is not in the supported set." >&2
        echo "[dkms-pre-build] Supported: 6.8.x (Ubuntu 24.04 GA)," >&2
        echo "[dkms-pre-build]            6.11.x (24.04.1 HWE)," >&2
        echo "[dkms-pre-build]            6.14.x (24.04.2 HWE)," >&2
        echo "[dkms-pre-build]            7.0.x  (Ubuntu 26.04 GA)." >&2
        echo "[dkms-pre-build] Add a vendor/<name>/ + patches/<name>/ pair," >&2
        echo "[dkms-pre-build] then add a case branch above." >&2
        exit 1
        ;;
esac

UBUNTU_VENDOR_DIR="$PWD/vendor/$TARGET"
OE_VENDOR_DIR="$PWD/vendor/openeuler"
PATCHES_DIR="$PWD/patches/$TARGET"
COMPAT_DIR="$PWD/compat"
SRC_DIR="$PWD/src"

if [[ ! -d "$PATCHES_DIR" ]]; then
    echo "[dkms-pre-build] ERROR: patches dir $PATCHES_DIR missing for kernel $KVER" >&2
    exit 1
fi
if [[ ! -x scripts/build-src-tree.sh ]]; then
    echo "[dkms-pre-build] ERROR: missing scripts/build-src-tree.sh in $PWD" >&2
    exit 1
fi
if [[ ! -x scripts/fetch-vendor-ubuntu.sh ]]; then
    echo "[dkms-pre-build] ERROR: missing scripts/fetch-vendor-ubuntu.sh in $PWD" >&2
    exit 1
fi

# As of #16 the .deb no longer bundles the vendor kernel source.
# Materialise it here from the linux-source-X.Y.Z apt package on the
# user's machine. The fetcher errors with a clear "apt install ..."
# hint if the package isn't present.
echo "[dkms-pre-build] target=$TARGET kernel=$KVER"
echo "[dkms-pre-build] fetching vendor/$TARGET from local linux-source pkg"
scripts/fetch-vendor-ubuntu.sh "$TARGET"

if [[ ! -d "$UBUNTU_VENDOR_DIR" ]]; then
    # Should be unreachable — the fetcher would have errored first.
    echo "[dkms-pre-build] ERROR: vendor dir $UBUNTU_VENDOR_DIR still missing after fetch" >&2
    exit 1
fi

scripts/build-src-tree.sh \
    "$UBUNTU_VENDOR_DIR" \
    "$OE_VENDOR_DIR" \
    "$PATCHES_DIR" \
    "$COMPAT_DIR" \
    "$SRC_DIR"
