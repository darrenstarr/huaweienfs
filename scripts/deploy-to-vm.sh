#!/usr/bin/env bash
# deploy-to-vm.sh - rsync the project to the test VM (enfs-dev).
#
# Usage: deploy-to-vm.sh USER@HOST DEST_PATH
#
# Excludes git metadata, build artefacts, and the (large) vendor/openeuler
# tree by default - the VM only needs src/, scripts/, debian/, dkms.conf.in
# and the Makefile for builds. Pass -v in $RSYNC_OPTS for chatty output.

set -euo pipefail

VM_HOST="${1:?host}"
VM_PATH="${2:?path}"
RSYNC_OPTS="${RSYNC_OPTS:-}"

ssh "$VM_HOST" "mkdir -p $VM_PATH"

# shellcheck disable=SC2086
rsync -a --delete $RSYNC_OPTS \
    --exclude='.git' \
    --exclude='*.ko' --exclude='*.o' --exclude='.*.cmd' \
    --exclude='*.mod*' --exclude='Module.symvers' --exclude='modules.order' \
    --exclude='vendor/openeuler' \
    --include='vendor/' --include='vendor/openeuler/UPSTREAM-REVISION' \
    "$(dirname "$0")/.."/ "$VM_HOST:$VM_PATH/"

echo "[deploy] synced to $VM_HOST:$VM_PATH"
ssh "$VM_HOST" "ls -la $VM_PATH"
