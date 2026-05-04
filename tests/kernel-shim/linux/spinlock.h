/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/spinlock.h>.
 *
 * spinlock_t maps to pthread_mutex_t. Even though tests are single-
 * threaded today, using a real lock means the source-under-test
 * actually exercises the lock paths, and if we ever add threaded
 * tests they'll Just Work.
 */
#ifndef _LINUX_SPINLOCK_H
#define _LINUX_SPINLOCK_H

#include <pthread.h>
#include <linux/compiler.h>

typedef struct {
    pthread_mutex_t m;
} spinlock_t;

#define DEFINE_SPINLOCK(name)  spinlock_t name = { .m = PTHREAD_MUTEX_INITIALIZER }

static inline void spin_lock_init(spinlock_t *l)   { pthread_mutex_init(&l->m, NULL); }
static inline void spin_lock(spinlock_t *l)        { pthread_mutex_lock(&l->m); }
static inline void spin_unlock(spinlock_t *l)      { pthread_mutex_unlock(&l->m); }
static inline void spin_lock_bh(spinlock_t *l)     { pthread_mutex_lock(&l->m); }
static inline void spin_unlock_bh(spinlock_t *l)   { pthread_mutex_unlock(&l->m); }
static inline void spin_lock_irq(spinlock_t *l)    { pthread_mutex_lock(&l->m); }
static inline void spin_unlock_irq(spinlock_t *l)  { pthread_mutex_unlock(&l->m); }

#define spin_lock_irqsave(l, flags)      do { (flags) = 0; spin_lock(l); } while (0)
#define spin_unlock_irqrestore(l, flags) do { (void)(flags); spin_unlock(l); } while (0)

#endif /* _LINUX_SPINLOCK_H */
