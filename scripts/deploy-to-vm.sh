#!/usr/bin/env bash
# deploy-to-vm.sh - rsync the project to the test VM (enfs-dev).
#
# Usage: deploy-to-vm.sh USER@HOST DEST_PATH
#
# Excludes git metadata and build artefacts. Includes all vendor/* trees
# so `make port` (run as part of `make modules`) can rebuild src/ from
# vendored Ubuntu source + patches + OE-only files. Total vendor/ is
# ~7 MB which is trivial. Pass -v in $RSYNC_OPTS for chatty output.

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
    "$(dirname "$0")/.."/ "$VM_HOST:$VM_PATH/"

echo "[deploy] synced to $VM_HOST:$VM_PATH"
ssh "$VM_HOST" "ls -la $VM_PATH"
