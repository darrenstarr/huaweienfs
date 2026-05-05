#!/bin/bash
# fork-sunrpc.sh — produce vendor/esunrpc/ from an upstream kernel
# source tree by:
#   1. Extracting the client-only subset of net/sunrpc/ + the headers
#      we need from include/linux/sunrpc/
#   2. Renaming every EXPORT_SYMBOL'd identifier to esunrpc_<original>
#      so the resulting esunrpc.ko coexists with stock sunrpc.ko
#      without symbol collision
#   3. Rewriting #include paths from <linux/sunrpc/X.h> to <esunrpc/X.h>
#
# This script is the source of truth for the fork. Re-run it whenever
# the upstream pin (vendor/esunrpc/UPSTREAM-REVISION) bumps; review
# the diff like any other update.
#
# See docs/internals/15-esunrpc-fork.md for design rationale.
#
# Usage:
#   bash scripts/fork-sunrpc.sh <upstream_kernel_src_dir>
#
# Idempotent: rerunning with the same input replaces vendor/esunrpc/
# with the same content.
set -euo pipefail

UPSTREAM_SRC="${1:-}"
if [[ -z "$UPSTREAM_SRC" ]]; then
    echo "usage: $0 <upstream_kernel_src_dir>" >&2
    echo "       (a directory containing net/sunrpc/ and include/linux/sunrpc/)" >&2
    exit 2
fi
if [[ ! -d "$UPSTREAM_SRC/net/sunrpc" ]] || [[ ! -d "$UPSTREAM_SRC/include/linux/sunrpc" ]]; then
    echo "ERROR: $UPSTREAM_SRC does not look like a kernel source tree" >&2
    exit 2
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DST="$ROOT/vendor/esunrpc"
WORK="$(mktemp -d -t fork-sunrpc.XXXXXX)"
trap 'rm -rf "$WORK"' EXIT

# Files to fork, by category. The trim list (server, GSS, backchannel,
# debugfs/sysfs/sysctl, cache, rpc_pipe) is documented in
# docs/internals/15-esunrpc-fork.md §15.3 — re-evaluate when the kernel
# version bumps.
SRCS_C="clnt.c xprt.c xprtsock.c xprtmultipath.c sched.c xdr.c \
        auth.c auth_unix.c auth_null.c auth_tls.c addr.c rpcb_clnt.c \
        timer.c socklib.c rpc_pipe.c cache.c stats.c \
        sysfs.c sysctl.c debugfs.c \
        svcauth_unix.c backchannel_rqst.c"
SRCS_H="socklib.h sunrpc.h netns.h fail.h sysfs.h"
# Why svcauth_unix.c is included (it's named svc* but is dual-use):
# clnt.c needs ip_map_cache_create/destroy and unix_gid_cache_create/
# destroy from this file at module init for AUTH_SYS auth caches. The
# truly server-only counterparts (svc.c, svc_xprt.c, svcsock.c,
# svcauth.c) are NOT included — code paths that would call them are
# stripped from esunrpc_syms.c by the post-rename surgery below.
HDRS="addr.h auth.h bc_xprt.h cache.h clnt.h debug.h gss_api.h \
      metrics.h msg_prot.h rpc_pipe_fs.h sched.h stats.h \
      svc.h svc_xprt.h svcauth.h svcsock.h \
      timer.h types.h xdr.h xprt.h xprtmultipath.h xprtsock.h"
# Note on the svc*/gss headers: the fork is client-only, but several
# of the forked .c files include these headers for type declarations
# (e.g., struct svc_sock for backchannel-socket pointers in xprtsock.c).
# We pull the headers so the includes resolve at compile time; we do
# NOT pull or link the server-side .c files. Code paths that would
# call into server symbols (svc_init_xprt_sock etc. in the original
# sunrpc_syms.c) are stripped by hand in the renamed esunrpc_syms.c.
# Module-init source: forked from sunrpc_syms.c into a renamed file
# that registers esunrpc as its own module.
SYMS_C="sunrpc_syms.c"

mkdir -p "$WORK/net/esunrpc" "$WORK/include/esunrpc"
for f in $SRCS_C $SRCS_H $SYMS_C; do
    cp "$UPSTREAM_SRC/net/sunrpc/$f" "$WORK/net/esunrpc/"
done
for f in $HDRS; do
    cp "$UPSTREAM_SRC/include/linux/sunrpc/$f" "$WORK/include/esunrpc/"
done

echo "[fork-sunrpc] copied $(find "$WORK" -name '*.[ch]' | wc -l) files"

# ---------------------------------------------------------------------
# Rename rules. Order matters:
#   1) Path renames must happen before identifier renames (same regex
#      could otherwise eat substrings).
#   2) Internal "sunrpc.h" -> "esunrpc.h" before identifier renames.
#   3) Identifier renames are the bulk of the change.
# ---------------------------------------------------------------------

# 1. Header include paths: <linux/sunrpc/X.h> -> <esunrpc/X.h>
find "$WORK" -name '*.[ch]' -exec sed -i \
    's|<linux/sunrpc/|<esunrpc/|g' {} +

# 1b. Header guards: _SUNRPC_*_H_ -> _ESUNRPC_*_H_
# Required because the stock kernel's <linux/sunrpc/X.h> uses the
# same guard names; if any transitively-included kernel header pulls
# in stock <linux/sunrpc/xdr.h> first, that defines _SUNRPC_XDR_H_
# and our <esunrpc/xdr.h> becomes a no-op include — symbols never get
# declared and call sites fail with "implicit declaration".
# Use | as sed delimiter to avoid escaping the / in #endif comments.
find "$WORK" -name '*.h' -exec sed -i \
    -e 's|\b_SUNRPC_|_ESUNRPC_|g' {} +

# 2. Internal sunrpc.h -> esunrpc.h
mv "$WORK/net/esunrpc/sunrpc.h" "$WORK/net/esunrpc/esunrpc.h"
find "$WORK" -name '*.[ch]' -exec sed -i \
    's|"sunrpc\.h"|"esunrpc.h"|g' {} +

# 3. Rename sunrpc_syms.c -> esunrpc_syms.c
mv "$WORK/net/esunrpc/sunrpc_syms.c" "$WORK/net/esunrpc/esunrpc_syms.c"

# 4. Discover all exported symbols. These (and their internal callers
# in our forked tree) get renamed with the esunrpc_ prefix.
mapfile -t EXPORTS < <(
    grep -h 'EXPORT_SYMBOL' "$WORK"/net/esunrpc/*.c | \
        sed -E 's/.*EXPORT_SYMBOL[_GPL]*\(([A-Za-z0-9_]+)\).*/\1/' | \
        sort -u
)
echo "[fork-sunrpc] discovered ${#EXPORTS[@]} exported symbols to rename"

# Build a single sed program that does all renames in one pass per file,
# so we don't pay the cost of N invocations.
SED_RULES="$WORK/.rename-rules.sed"
: > "$SED_RULES"
for sym in "${EXPORTS[@]}"; do
    # \b word boundary so 'rpc_call_sync' doesn't match inside
    # 'foo_rpc_call_sync_bar'. Skip names already prefixed (rerun safety).
    if [[ "$sym" == esunrpc_* ]]; then
        continue
    fi
    printf 's/\\b%s\\b/esunrpc_%s/g\n' "$sym" "$sym" >> "$SED_RULES"
done

find "$WORK" -name '*.[ch]' -exec sed -i -f "$SED_RULES" {} +

# 4b. Drop the kernel-tracepoint include chain.
# <trace/events/sunrpc.h> drags in stock <linux/sunrpc/svc.h> ->
# <linux/sunrpc/xdr.h>, which redefines our struct types. We don't
# need tracepoints for the esunrpc fork (PR 1 — see chapter 15 §15.7);
# strip the include and gate any CREATE_TRACE_POINTS / trace_* call
# by relying on the stub header below.
find "$WORK/net/esunrpc" -name '*.c' -exec sed -i \
    -e 's|^#include[ \t]\+<trace/events/sunrpc\.h>|#include "esunrpc-trace-stub.h"|' \
    -e '/^#define CREATE_TRACE_POINTS/d' \
    {} +

# 4c. Stub header: every trace_* call inside our forked .c becomes a
# no-op. This avoids having to per-file edit out the trace_* sites.
cat > "$WORK/net/esunrpc/esunrpc-trace-stub.h" <<'EOF'
/* SPDX-License-Identifier: GPL-2.0 */
/* Stub for <trace/events/sunrpc.h>. Disables all sunrpc tracepoints
 * inside the esunrpc fork. Real tracepoints can be added back in a
 * later PR (see chapter 15 §15.7 / PR 5+). */
#ifndef _ESUNRPC_TRACE_STUB_H
#define _ESUNRPC_TRACE_STUB_H
#include <linux/types.h>
/* Define a variadic no-op for any trace_*() macro call. The kernel's
 * tracepoint API expands to function calls; we replace them with empty
 * statements via a vararg-eating macro so call sites compile unchanged. */
#define ESUNRPC_TRACE_NOOP(...) do { (void)(0); } while (0)
EOF
# Append a stub for every trace_* identifier referenced by the forked
# sources. Discover the set, then emit a #define for each.
grep -hoE 'trace_[a-zA-Z0-9_]+' "$WORK"/net/esunrpc/*.c | sort -u | \
    while read -r name; do
        printf '#define %s(...) ESUNRPC_TRACE_NOOP(__VA_ARGS__)\n' "$name"
    done >> "$WORK/net/esunrpc/esunrpc-trace-stub.h"
echo "#endif" >> "$WORK/net/esunrpc/esunrpc-trace-stub.h"

# 4d. Tiny stub for server-side helpers our kept files reference but
# we can't fork (the implementing .c files are server-only). Linker
# would otherwise fail with "undefined reference" at modpost time.
cat > "$WORK/net/esunrpc/esunrpc_server_stubs.c" <<'EOF'
// SPDX-License-Identifier: GPL-2.0-only
/*
 * Server-side helper stubs for the client-only esunrpc.ko fork.
 *
 * sysctl.c calls svc_print_xprts() to render the list of registered
 * server transports for /proc. esunrpc has no server side so the
 * "registered transports" list is permanently empty. The stub
 * returns 0 (zero bytes written) which sysctl.c handles correctly.
 *
 * Add other server stubs here as they show up at modpost time.
 */
#include <linux/types.h>
#include <linux/export.h>

int svc_print_xprts(char *buf, int maxlen)
{
	(void)buf;
	(void)maxlen;
	return 0;
}
EXPORT_SYMBOL_GPL(svc_print_xprts);
EOF

# 4e. Disambiguate global-namespace resources from stock sunrpc.
# Stock sunrpc and esunrpc would both register the same /proc paths,
# filesystem names, workqueue names, slab caches, and debugfs dirs;
# stock wins (it loads at boot via fs_initcall) and esunrpc fails to
# load with "Device or resource busy" or "already registered".
# Rename each colliding name with an "e" prefix so both stacks coexist.

# /proc/net/rpc -> /proc/net/esunrpc
sed -i \
    -e 's|"rpc", net->proc_net|"esunrpc", net->proc_net|g' \
    -e 's|remove_proc_entry("rpc", net->proc_net)|remove_proc_entry("esunrpc", net->proc_net)|g' \
    "$WORK/net/esunrpc/stats.c"

# rpc_pipefs filesystem -> esunrpc_pipefs (and inode-cache slab,
# module aliases). Without this, register_filesystem() returns -EBUSY.
sed -i \
    -e 's|"rpc_pipefs"|"esunrpc_pipefs"|g' \
    -e 's|"rpc_inode_cache"|"esunrpc_inode_cache"|g' \
    "$WORK/net/esunrpc/rpc_pipe.c"

# rpciod workqueue + rpc_tasks/rpc_buffers slab caches.
sed -i \
    -e 's|"rpciod"|"esunrpciod"|g' \
    -e 's|"rpc_tasks"|"esunrpc_tasks"|g' \
    -e 's|"rpc_buffers"|"esunrpc_buffers"|g' \
    "$WORK/net/esunrpc/sched.c"

# debugfs dirs ("rpc_clnt", "rpc_xprt") and the top "sunrpc" dir.
sed -i \
    -e 's|debugfs_create_dir("rpc_clnt"|debugfs_create_dir("esunrpc_clnt"|g' \
    -e 's|debugfs_create_dir("rpc_xprt"|debugfs_create_dir("esunrpc_xprt"|g' \
    -e 's|debugfs_create_dir("sunrpc"|debugfs_create_dir("esunrpc"|g' \
    "$WORK/net/esunrpc/debugfs.c"

# /sys/kernel/sunrpc kset
sed -i \
    -e 's|kset_create_and_add("sunrpc"|kset_create_and_add("esunrpc"|g' \
    "$WORK/net/esunrpc/sysfs.c"

# 5. Post-rename surgery on esunrpc_syms.c. Two changes:
#   (a) Bump MODULE_DESCRIPTION so `modinfo esunrpc` makes it obvious
#       this is the fork, not stock sunrpc.
#   (b) Strip the two calls that bring in server-side svcsock symbols
#       (svc_init_xprt_sock / svc_cleanup_xprt_sock — server only;
#       not in the client-only fork). Also strip auth_domain_cleanup
#       (server-side svcauth.c).
sed -i \
    -e 's|MODULE_DESCRIPTION("[^"]*")|MODULE_DESCRIPTION("Forked SunRPC client for enfs (no symbol collision with stock sunrpc.ko)")|' \
    -e '/svc_init_xprt_sock();/d' \
    -e '/svc_cleanup_xprt_sock();/d' \
    -e '/auth_domain_cleanup();/d' \
    "$WORK/net/esunrpc/esunrpc_syms.c"

# Replace destination atomically.
rm -rf "$DST/net" "$DST/include"
mkdir -p "$DST"
mv "$WORK/net" "$WORK/include" "$DST/"

# Persist the rule list so a later kernel bump's diff is easier to read.
cp "$SED_RULES" "$DST/.rename-rules.sed"

echo "[fork-sunrpc] complete:"
echo "  source:   $(find "$DST/net" -name '*.c' | wc -l) .c files"
echo "  internal: $(find "$DST/net" -name '*.h' | wc -l) .h files"
echo "  public:   $(find "$DST/include" -name '*.h' | wc -l) public headers"
echo "  renamed:  ${#EXPORTS[@]} exported symbols"
echo "  output:   $DST"
