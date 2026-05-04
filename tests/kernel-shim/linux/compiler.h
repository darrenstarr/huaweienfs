/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/compiler.h>.
 *
 * The real kernel header carries sparse annotations and compiler
 * intrinsics. We strip them all to nothing for userspace builds,
 * so source compiles without sparse and without the kernel's
 * compiler-attribute family.
 */
#ifndef _LINUX_COMPILER_H
#define _LINUX_COMPILER_H

#define __rcu
#define __force
#define __user
#define __kernel
#define __iomem
#define __must_check
#define __read_mostly
#define __init
#define __exit
#define __cold
#define __maybe_unused      __attribute__((unused))
#ifndef __always_inline
#define __always_inline     inline __attribute__((always_inline))
#endif
#define __packed            __attribute__((packed))
#define __aligned(x)        __attribute__((aligned(x)))

#define likely(x)           __builtin_expect(!!(x), 1)
#define unlikely(x)         __builtin_expect(!!(x), 0)

/* Treated as plain reads/writes in single-threaded userspace tests.
 * Concurrency tests would need to revisit. */
#define READ_ONCE(x)        (*(volatile typeof(x) *)&(x))
#define WRITE_ONCE(x, val)  (*(volatile typeof(x) *)&(x) = (val))

#define smp_load_acquire(p)  READ_ONCE(*(p))
#define smp_store_release(p, v) WRITE_ONCE(*(p), (v))
#define smp_mb()             __asm__ __volatile__("" ::: "memory")
#define smp_rmb()            smp_mb()
#define smp_wmb()            smp_mb()
#define barrier()            __asm__ __volatile__("" ::: "memory")

#endif /* _LINUX_COMPILER_H */
