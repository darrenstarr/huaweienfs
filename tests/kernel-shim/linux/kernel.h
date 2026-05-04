/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/kernel.h>.
 *
 * Provides container_of, min/max, and a few common helpers. Most of
 * the real kernel.h surface (panic, printk levels, etc.) isn't
 * relevant here.
 */
#ifndef _LINUX_KERNEL_H
#define _LINUX_KERNEL_H

#include <linux/compiler.h>
#include <linux/types.h>

#define container_of(ptr, type, member) ({                              \
    const typeof(((type *)0)->member) *__mptr = (ptr);                  \
    (type *)((char *)__mptr - offsetof(type, member)); })

#define ARRAY_SIZE(a)  (sizeof(a) / sizeof((a)[0]))

#ifndef min
#define min(a, b)      ((a) < (b) ? (a) : (b))
#endif
#ifndef max
#define max(a, b)      ((a) > (b) ? (a) : (b))
#endif

#define BUG_ON(cond)   do { if (cond) { fprintf(stderr, "BUG_ON: %s\n", #cond); abort(); } } while (0)
#define WARN_ON(cond)  ((cond) ? (fprintf(stderr, "WARN_ON: %s\n", #cond), 1) : 0)
#define WARN_ON_ONCE(cond)  WARN_ON(cond)

#define BUILD_BUG_ON(cond) ((void)sizeof(char[1 - 2*!!(cond)]))

#endif /* _LINUX_KERNEL_H */
