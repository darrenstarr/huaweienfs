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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)

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
 * the cursor *without advancing*. Stock Ubuntu only exposes
 * xprt_iter_get_next() (advance + return). We approximate by calling
 * get_next; the semantic difference (one extra advance per call) may
 * shift load slightly but does not break correctness because the
 * iterator is round-robin and enfs only cares about *some* live
 * transport, not specifically the "current" one.
 *
 * TODO: if a benchmark shows noticeable load imbalance, revisit by
 * porting OE's xprt_iter_get_helper() into compat/.
 */
static inline struct rpc_xprt *enfs_compat_xprt_iter_get_xprt(struct rpc_xprt_iter *xpi)
{
	return xprt_iter_get_next(xpi);
}
#define xprt_iter_get_xprt(xpi) enfs_compat_xprt_iter_get_xprt(xpi)

#endif /* >= 7.0.0 */

#endif /* _ENFS_COMPAT_H_ */
