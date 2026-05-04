/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/refcount.h>.
 *
 * refcount_t backs onto an atomic; same simplified semantics as kref.
 */
#ifndef _LINUX_REFCOUNT_H
#define _LINUX_REFCOUNT_H

#include <linux/atomic.h>

typedef struct { atomic_t refs; } refcount_t;

#define REFCOUNT_INIT(n)  { .refs = ATOMIC_INIT(n) }

static inline void refcount_set(refcount_t *r, unsigned int n) { atomic_set(&r->refs, (int)n); }
static inline unsigned int refcount_read(const refcount_t *r)  { return (unsigned int)atomic_read(&r->refs); }
static inline void refcount_inc(refcount_t *r) { atomic_inc(&r->refs); }
static inline bool refcount_dec_and_test(refcount_t *r) { return atomic_dec_return(&r->refs) == 0; }

#endif /* _LINUX_REFCOUNT_H */
