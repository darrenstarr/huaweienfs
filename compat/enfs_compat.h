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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(7, 0, 0)

/* TODO: nfs_fs_context gained `int lock_status;` (fs/nfs/internal.h).
 * enfs source touches fs_context fields by name, so this is additive
 * and should be a no-op — but verify the offsets used by enfs_adapter.c
 * during smoke build.
 */

/* TODO: audit struct rpc_clnt for new/removed fields between 6.6 and 7.0
 * (clnt.h grew from 279 → 301 lines in OE; some of that may be
 * enfs-only additions, the rest is upstream drift). */

/* TODO: audit struct rpc_task — include/linux/sunrpc/sched.h has an
 * IS_ENABLED(CONFIG_SUNRPC_ENFS) hunk that adds fields. Confirm the
 * surrounding struct layout still matches Ubuntu's 7.0 sched.h.  */

#endif /* >= 7.0.0 */

#endif /* _ENFS_COMPAT_H_ */
