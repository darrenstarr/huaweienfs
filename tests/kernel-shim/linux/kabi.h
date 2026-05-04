/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/kabi.h>.
 *
 * KABI_RESERVE() and friends are OpenEuler/RHEL ABI-compatibility
 * macros that reserve struct fields for future use without breaking
 * the kABI. Userspace tests don't care; expand to nothing.
 */
#ifndef _LINUX_KABI_H
#define _LINUX_KABI_H

#define KABI_RESERVE(n)
#define KABI_USE(n, fn)
#define KABI_REPLACE(orig, new)
#define KABI_EXTEND(fn)
#define KABI_DEPRECATE(fn, name)
#define KABI_FILL_HOLE(fn)
#define KABI_BROKEN_INSERT(fn)

#endif /* _LINUX_KABI_H */
