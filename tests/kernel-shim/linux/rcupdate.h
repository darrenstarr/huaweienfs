/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/rcupdate.h>.
 *
 * RCU is a no-op in single-threaded tests. rcu_dereference yields the
 * pointer as-is; rcu_assign_pointer is a plain assignment.
 */
#ifndef _LINUX_RCUPDATE_H
#define _LINUX_RCUPDATE_H

#include <linux/compiler.h>

#define rcu_read_lock()       ((void)0)
#define rcu_read_unlock()     ((void)0)
#define rcu_read_lock_bh()    ((void)0)
#define rcu_read_unlock_bh()  ((void)0)

#define rcu_dereference(p)               (p)
#define rcu_dereference_protected(p, c)  (p)
#define rcu_dereference_check(p, c)      (p)
#define rcu_dereference_raw(p)           (p)

#define rcu_assign_pointer(p, v)         ((p) = (v))

#define synchronize_rcu()                ((void)0)
#define call_rcu(head, fn)               (fn(head))

struct rcu_head {
    void *next;
    void (*func)(struct rcu_head *);
};

#endif /* _LINUX_RCUPDATE_H */
