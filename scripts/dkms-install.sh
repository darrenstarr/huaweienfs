#!/usr/bin/env bash
# dkms-install.sh — stage the project under /usr/src/PROJECT-VERSION/
# and run `dkms add/build/install`.
#
# Usage: dkms-install.sh PROJECT VERSION PROJECT_ROOT DKMS_CONF
#
# Unlike a normal DKMS source, we ship the WHOLE project (vendor/,
# patches/, compat/, scripts/, Kbuild) — not just src/ — because the
# DKMS PRE_BUILD hook regenerates src/ from those for the kernel
# we're building against.
#
# Re-runs are safe: if the version is already registered it gets
# removed first.

set -euo pipefail

PROJECT="${1:?project}"
VERSION="${2:?version}"
PROJECT_ROOT="${3:?project root}"
DKMS_CONF="${4:?dkms.conf path}"

DEST="/usr/src/${PROJECT}-${VERSION}"

if dkms status -m "$PROJECT" -v "$VERSION" 2>/dev/null | grep -q "$PROJECT"; then
    echo "[dkms] removing existing $PROJECT/$VERSION"
    sudo dkms remove -m "$PROJECT" -v "$VERSION" --all || true
fi

echo "[dkms] staging $PROJECT_ROOT -> $DEST"
sudo rm -rf "$DEST"
sudo mkdir -p "$DEST"

# Copy everything DKMS needs to rebuild against an arbitrary kernel:
#   - vendor/ubuntu-7.0/    stock Ubuntu kernel files we patch
#   - vendor/openeuler/     OE-only enfs sources (enfs/, *_adapter.*)
#   - patches/ubuntu-7.0/   patches series
#   - compat/               kernel-version shim header
#   - scripts/              build-src-tree.sh + dkms-pre-build.sh
#   - Kbuild                top-level Kbuild
#
# We deliberately do NOT copy:
#   - src/    (regenerated each rebuild)
#   - .git, secrets, CLAUDE.md, debian/, docs/  (not needed at build)
sudo rsync -a \
    --exclude='.git' \
    --exclude='secrets' \
    --exclude='CLAUDE.md' \
    --exclude='src' \
    --exclude='debian' \
    --exclude='docs' \
    --exclude='*.ko' \
    --exclude='.*.cmd' \
    --exclude='*.o' \
    --exclude='*.mod*' \
    --exclude='Module.symvers' \
    "$PROJECT_ROOT/" "$DEST/"
sudo cp "$DKMS_CONF" "$DEST/dkms.conf"

echo "[dkms] add"
sudo dkms add -m "$PROJECT" -v "$VERSION"
echo "[dkms] build (against $(uname -r))"
sudo dkms build -m "$PROJECT" -v "$VERSION"
echo "[dkms] install (--force overrides stock unversioned nfs_acl)"
sudo dkms install --force -m "$PROJECT" -v "$VERSION"
echo "[dkms] status:"
dkms status -m "$PROJECT" -v "$VERSION"
