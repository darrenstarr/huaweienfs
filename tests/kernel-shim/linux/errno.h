/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <linux/errno.h>.
 *
 * The kernel's linux/errno.h pulls in asm-generic/errno.h for all
 * the E* codes. Pull those in directly here — using libc's <errno.h>
 * doesn't help because glibc routes back through linux/errno.h
 * which would resolve to *this* file (a loop).
 *
 * asm-generic/errno*.h ship with linux-libc-dev on every Ubuntu/
 * Debian system and define EINVAL, EOPNOTSUPP, ENOMEM, etc. in the
 * format the SUT expects.
 */
#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#endif
