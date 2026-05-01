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
/* No structural shims needed yet — patches handle the drift. Add new
 * blocks here as `make build-on-vm` surfaces them. */
#endif /* >= 7.0.0 */

#endif /* _ENFS_COMPAT_H_ */
