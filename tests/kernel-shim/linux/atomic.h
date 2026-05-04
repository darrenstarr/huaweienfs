/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/atomic.h>.
 *
 * Maps atomic_t / atomic_long_t to C11 _Atomic. Tests are single-
 * threaded so atomicity is not strictly required, but using real
 * atomics is free and means the operations are actually defined
 * the way the kernel expects.
 */
#ifndef _LINUX_ATOMIC_H
#define _LINUX_ATOMIC_H

#include <stdatomic.h>

typedef struct { atomic_int      counter; } atomic_t;
typedef struct { atomic_long     counter; } atomic_long_t;

#define ATOMIC_INIT(v)        { .counter = (v) }
#define ATOMIC_LONG_INIT(v)   { .counter = (v) }

static inline int  atomic_read(const atomic_t *a)        { return atomic_load(&a->counter); }
static inline void atomic_set(atomic_t *a, int v)         { atomic_store(&a->counter, v); }
static inline void atomic_inc(atomic_t *a)                { atomic_fetch_add(&a->counter, 1); }
static inline void atomic_dec(atomic_t *a)                { atomic_fetch_sub(&a->counter, 1); }
static inline int  atomic_inc_return(atomic_t *a)         { return atomic_fetch_add(&a->counter, 1) + 1; }
static inline int  atomic_dec_return(atomic_t *a)         { return atomic_fetch_sub(&a->counter, 1) - 1; }

static inline long atomic_long_read(const atomic_long_t *a) { return atomic_load(&a->counter); }
static inline void atomic_long_set(atomic_long_t *a, long v) { atomic_store(&a->counter, v); }
static inline void atomic_long_inc(atomic_long_t *a)         { atomic_fetch_add(&a->counter, 1); }
static inline void atomic_long_dec(atomic_long_t *a)         { atomic_fetch_sub(&a->counter, 1); }

#endif /* _LINUX_ATOMIC_H */
