/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/kref.h>.
 *
 * Tests construct krefs by hand to control kref_read() return values.
 * Real ref-count semantics aren't needed for selection-logic tests;
 * if a test needs to model "this xprt is going away", it just sets
 * the counter to 0.
 */
#ifndef _LINUX_KREF_H
#define _LINUX_KREF_H

#include <linux/atomic.h>

struct kref {
    atomic_t refcount;
};

#define KREF_INIT(v)  { .refcount = ATOMIC_INIT(v) }

static inline void kref_init(struct kref *k)   { atomic_set(&k->refcount, 1); }
static inline int  kref_read(const struct kref *k) { return atomic_read(&k->refcount); }
static inline void kref_get(struct kref *k)    { atomic_inc(&k->refcount); }
static inline int  kref_put(struct kref *k, void (*release)(struct kref *)) {
    if (atomic_dec_return(&k->refcount) == 0) { if (release) release(k); return 1; }
    return 0;
}

#endif /* _LINUX_KREF_H */
