/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/slab.h>.
 *
 * kmalloc / kzalloc / kfree route to malloc/calloc/free. Flags are
 * ignored. Tests that care about allocation failure can intercept
 * via LD_PRELOAD or by extending this shim later.
 */
#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <stdlib.h>
#include <string.h>
#include <linux/types.h>

#define GFP_KERNEL  0
#define GFP_ATOMIC  0
#define GFP_NOFS    0
#define GFP_NOIO    0
#define __GFP_ZERO  0

static inline void *kmalloc(size_t size, gfp_t flags)  { (void)flags; return malloc(size); }
static inline void *kzalloc(size_t size, gfp_t flags)  { (void)flags; return calloc(1, size); }
static inline void *kcalloc(size_t n, size_t size, gfp_t flags) { (void)flags; return calloc(n, size); }
static inline void  kfree(const void *p)               { free((void *)p); }
static inline void *krealloc(const void *p, size_t size, gfp_t flags) { (void)flags; return realloc((void *)p, size); }

#endif /* _LINUX_SLAB_H */
