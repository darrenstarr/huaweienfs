#!/usr/bin/env bash
# sync-from-openeuler.sh - refresh vendor/openeuler/ from a local OpenEuler
# kernel checkout, possibly on a remote build host.
#
# Usage:  sync-from-openeuler.sh [REMOTE:]PATH_TO_OE_KERNEL
# Examples:
#   scripts/sync-from-openeuler.sh ./openeuler-kernel
#   scripts/sync-from-openeuler.sh build-host:/srv/kernel-work/openeuler/OLK-6.6
#
# Locally, clone OpenEuler with:
#   git clone --depth 1 --single-branch --branch OLK-6.6 \
#       https://gitee.com/openeuler/kernel.git ./openeuler-kernel
#
# Updates vendor/openeuler/UPSTREAM-REVISION with the new commit metadata.
# Vendoring is intentionally limited to the files enfs needs - keeping the
# checkout small (~900 KiB) and the diff against Ubuntu stock readable.

set -euo pipefail

OE="${1:?path to OpenEuler kernel checkout, possibly remote (host:path)}"
HERE="$(cd "$(dirname "$0")"/.. && pwd)"
DEST="$HERE/vendor/openeuler"

# Files we care about. Keep this list in sync with the survey results in
# docs/PORTING-NOTES.md.
PATHS=(
    fs/nfs/enfs
    fs/nfs/enfs_adapter.c
    fs/nfs/enfs_adapter.h
    fs/nfs/Kconfig
    fs/nfs/Makefile
    fs/nfs/super.c
    fs/nfs/fs_context.c
    fs/nfs/nfs3xdr.c
    fs/nfs/internal.h
    net/sunrpc/sunrpc_enfs_adapter.c
    net/sunrpc/clnt.c
    net/sunrpc/xprt.c
    net/sunrpc/Kconfig
    net/sunrpc/Makefile
    include/linux/nfs_fs_sb.h
    include/linux/nfs_xdr.h
    include/linux/sunrpc/sched.h
    include/linux/sunrpc/clnt.h
    include/linux/sunrpc/sunrpc_enfs_adapter.h
)

mkdir -p "$DEST"
rm -rf "$DEST"/{fs,net,include}

if [[ "$OE" == *:* ]]; then
    HOST="${OE%%:*}"; OEPATH="${OE#*:}"
    ssh "$HOST" "cd $OEPATH && tar -cf - ${PATHS[*]}" | tar -xf - -C "$DEST"
    META=$(ssh "$HOST" "cd $OEPATH && git log -1 --pretty='format:%H|%s|%cI'" 2>/dev/null)
else
    (cd "$OE" && tar -cf - "${PATHS[@]}") | tar -xf - -C "$DEST"
    META=$(cd "$OE" && git log -1 --pretty='format:%H|%s|%cI' 2>/dev/null)
fi

IFS='|' read -r SHA SUBJECT WHEN <<<"$META"
cat > "$DEST/UPSTREAM-REVISION" <<EOF
branch: OLK-6.6
remote: https://gitee.com/openeuler/kernel.git
commit: $SHA
subject: $SUBJECT
committed: $WHEN
vendored: $(date -u +%Y-%m-%dT%H:%M:%SZ)
# fetched_from: see secrets/ (local-only) for the source path
EOF

echo "[sync] updated vendor/openeuler/ from $OE"
cat "$DEST/UPSTREAM-REVISION"
