/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/rculist.h>.
 *
 * Single-threaded tests don't need RCU semantics; the macros forward
 * to the non-rcu variants from <linux/list.h>. Concurrency tests
 * would have to revisit.
 */
#ifndef _LINUX_RCULIST_H
#define _LINUX_RCULIST_H

#include <linux/list.h>

#define list_for_each_entry_rcu(pos, head, member) \
    list_for_each_entry(pos, head, member)

#define list_add_rcu(new, head)         list_add(new, head)
#define list_add_tail_rcu(new, head)    list_add_tail(new, head)
#define list_del_rcu(entry)             list_del(entry)

#endif /* _LINUX_RCULIST_H */
