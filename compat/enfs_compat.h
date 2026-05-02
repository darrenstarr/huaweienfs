/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * enfs_compat.h - kernel-version compatibility shims for porting the
 * OpenEuler enfs module from kernel 6.6 (OLK-6.6) to newer Linux kernels
 * (currently targeting Ubuntu 26.04 / kernel 7.0).
 *
 * Pulled in via -include or by ccflags-y += -I$(src)/compat in the
 * top-level Kbuild. Add #ifdef KERNEL_VERSION blocks here rather than
 * editing vendored sources line-by-line; that keeps `git diff` against
 * vendor/openeuler/ readable and makes future kernel rebases mechanical.
 */
#ifndef _ENFS_COMPAT_H_
#define _ENFS_COMPAT_H_

#include <linux/version.h>

/*
 * Known API drift from OLK-6.6 → Linux 7.0 that affects enfs.
 * Each block below is a TODO. Verify against the actual kernel headers
 * and either remove the block (no drift) or implement the shim.
 */

/*
 * NFSDBG_ENFS — debug-facility bit used by enfs source via
 * `ifdebug(ENFS)` / `nfs_debug & NFSDBG_ENFS`. OE adds it to
 * include/uapi/linux/nfs_fs.h as 0x10000; Ubuntu's stock UAPI doesn't
 * carry it. We define it here so any TU that includes nfs_fs.h before
 * this file gets the symbol via subsequent reference.
 *
 * Bit chosen to match OE (0x10000) so any saved /proc/sys debug masks
 * keep their meaning between OE and our build.
 */
#ifndef NFSDBG_ENFS
#define NFSDBG_ENFS	0x10000
#endif

/*
 * Shims below apply to every kernel target we currently support
 * (Ubuntu 6.8, 6.11, 6.14, 7.0). Each helper either declares a
 * function our patches export (so the upstream header doesn't need to
 * change), or stubs an OpenEuler-only helper that has no Ubuntu
 * equivalent. None of them clash with stock Ubuntu symbols on any
 * supported version, so a single block covers all targets.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtmultipath.h>

/* rpc_task_get_next_xprt() exists in Ubuntu 7.0 clnt.c but is `static`.
 * Patch 0016 removes the static keyword and adds EXPORT_SYMBOL_GPL so
 * enfs.ko can link to it. The kernel header doesn't declare it (since
 * it was static), so we forward-declare here. */
struct rpc_xprt *rpc_task_get_next_xprt(struct rpc_clnt *clnt);

/* Patch 0017 exports xprt_switch_add_xprt_locked from xprtmultipath.c. */
struct rpc_xprt;
struct rpc_xprt_switch;
void xprt_switch_add_xprt_locked(struct rpc_xprt_switch *xps, struct rpc_xprt *xprt);

/*
 * NFS3PROC_EXTEND — patch 0012 defines this inside fs/nfs/nfs3xdr.c
 * (because the UAPI header is out of scope to patch). enfs source
 * (fs/nfs/enfs/exten_call.c) needs to see it too. Define here so both
 * the encoder and the caller agree.
 *
 * Value matches OpenEuler's vendor/openeuler/include/uapi/linux/nfs3.h.
 */
#ifndef NFS3PROC_EXTEND
#define NFS3PROC_EXTEND		22
#endif

/*
 * rpc_clnt_test_xprt() — patch 0020 implements this in
 * net/sunrpc/clnt.c (OE-style); just declare it here.
 */
struct rpc_call_ops;
int rpc_clnt_test_xprt(struct rpc_clnt *clnt, struct rpc_xprt *xprt,
		       const struct rpc_call_ops *ops, void *data, int flags);

/*
 * rpc_localalladdr() — OE-specific helper that enumerates local-NIC
 * source addresses for the `localaddrs=` mount option's auto-bind
 * mode. Stub returns 0 (no addresses) so callers fall through to the
 * explicit IP list provided by the user. Mount still works.
 */
struct sockaddr;
static inline size_t enfs_compat_rpc_localalladdr(struct rpc_xprt *xprt,
		struct sockaddr *buf, size_t buflen)
{
	(void)xprt; (void)buf; (void)buflen;
	WARN_ONCE(1, "enfs: rpc_localalladdr stubbed (auto-bind unavailable; use explicit localaddrs=)");
	return 0;
}
#define rpc_localalladdr(x, b, l) enfs_compat_rpc_localalladdr(x, b, l)

/*
 * Stubs for fs/nfs/enfs/shard_route.c functions. shard_route.o is
 * dropped from the build (NLM-multipath needs lockd patches we haven't
 * done yet). Other enfs files still call into shard.h; provide
 * no-op stubs so the link succeeds.
 */
/* Use identifier-only #define so the symbols can also appear as
 * function pointers (e.g. in init-table struct initializers in
 * enfs_init.c). #define X(args) Y(args) breaks `&X` and `X` in
 * non-call contexts. */
static inline int enfs_compat_delete_clnt_shard_cache(struct rpc_clnt *clnt)
{ (void)clnt; return 0; }
#define enfs_delete_clnt_shard_cache enfs_compat_delete_clnt_shard_cache

struct rpc_task;
static inline void enfs_compat_shard_set_transport(struct rpc_task *t, struct rpc_clnt *c)
{ (void)t; (void)c; }
#define shard_set_transport enfs_compat_shard_set_transport

static inline int enfs_compat_enfs_shard_init(void)
{ return 0; }
#define enfs_shard_init enfs_compat_enfs_shard_init

static inline void enfs_compat_enfs_shard_exit(void)
{ }
#define enfs_shard_exit enfs_compat_enfs_shard_exit

static inline void enfs_compat_enfs_query_xprt_shard(struct rpc_clnt *c, struct rpc_xprt *x)
{ (void)c; (void)x; }
#define enfs_query_xprt_shard enfs_compat_enfs_query_xprt_shard

struct enfs_file_uuid;
static inline void enfs_compat_enfs_print_uuid(struct enfs_file_uuid *u)
{ (void)u; }
#define enfs_print_uuid enfs_compat_enfs_print_uuid

/*
 * rpc_xprt_switch_set_singular() — OE-specific. Configures the
 * xprt-iterator to "singular" mode (always return the same xprt).
 * Called by fs/nfs/enfs/enfs_roundrobin.c during failover-pinning.
 *
 * Ubuntu has no equivalent. We stub to a no-op for now: enfs's
 * round-robin still works, but the "pin to one path" optimization is
 * disabled. Logged with WARN_ONCE so it shows up in dmesg the first
 * time it would have fired.
 *
 * TODO: replicate OE's rpc_xprt_iter_singular by porting it into
 * compat/ as a small standalone iter_ops. Until then this stub is
 * graceful-degradation.
 */
struct rpc_xprt_switch;
static inline void enfs_compat_rpc_xprt_switch_set_singular(struct rpc_xprt_switch *xps)
{
	(void)xps;
	WARN_ONCE(1, "enfs: rpc_xprt_switch_set_singular stubbed (Ubuntu 7.0 has no singular iter)");
}
#define rpc_xprt_switch_set_singular(xps) enfs_compat_rpc_xprt_switch_set_singular(xps)

/*
 * xprt_iter_get_xprt() — OE returns the current xprt pointed to by
 * the cursor *without advancing*. Ubuntu 6.8 still ships this helper
 * (declared & exported via our patch). Ubuntu 6.14 and 7.0 dropped it
 * in favour of the advance-only xprt_iter_get_next(); we approximate
 * it here. The semantic difference (one extra advance per call) may
 * shift load slightly but does not break correctness because the
 * iterator is round-robin and enfs only cares about *some* live
 * transport, not specifically the "current" one.
 *
 * TODO: if a benchmark shows noticeable load imbalance, revisit by
 * porting OE's xprt_iter_get_helper() into compat/.
 */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
static inline struct rpc_xprt *enfs_compat_xprt_iter_get_xprt(struct rpc_xprt_iter *xpi)
{
	return xprt_iter_get_next(xpi);
}
#define xprt_iter_get_xprt(xpi) enfs_compat_xprt_iter_get_xprt(xpi)
#endif

#endif /* >= 6.8.0 */

#endif /* _ENFS_COMPAT_H_ */
