#!/usr/bin/env bash
# dkms-install.sh - install the staged sources under /usr/src/ and
# trigger dkms add/build/install for the running kernel.
#
# Usage: dkms-install.sh PROJECT VERSION SRC_DIR DKMS_CONF
#
# Re-runs are safe: if the version is already registered it gets
# removed first.

set -euo pipefail

PROJECT="${1:?project}"
VERSION="${2:?version}"
SRC_DIR="${3:?src dir}"
DKMS_CONF="${4:?dkms.conf path}"

DEST="/usr/src/${PROJECT}-${VERSION}"

if dkms status -m "$PROJECT" -v "$VERSION" 2>/dev/null | grep -q "$PROJECT"; then
    echo "[dkms] removing existing $PROJECT/$VERSION"
    sudo dkms remove -m "$PROJECT" -v "$VERSION" --all
fi

echo "[dkms] staging $SRC_DIR -> $DEST"
sudo rm -rf "$DEST"
sudo mkdir -p "$DEST"
sudo cp -a "$SRC_DIR"/. "$DEST"/
sudo cp "$DKMS_CONF" "$DEST/dkms.conf"

echo "[dkms] add"
sudo dkms add -m "$PROJECT" -v "$VERSION"
echo "[dkms] build"
sudo dkms build -m "$PROJECT" -v "$VERSION"
echo "[dkms] install"
sudo dkms install -m "$PROJECT" -v "$VERSION"
echo "[dkms] status:"
dkms status -m "$PROJECT" -v "$VERSION"
