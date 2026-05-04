/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/list.h>.
 *
 * Reimplements the kernel's intrusive doubly-linked list with the
 * same API. The kernel's list.h is small and self-contained — this
 * is essentially a direct port that compiles in userspace.
 */
#ifndef _LINUX_LIST_H
#define _LINUX_LIST_H

#include <linux/kernel.h>

struct list_head {
    struct list_head *next, *prev;
};

#define LIST_HEAD_INIT(name)  { &(name), &(name) }
#define LIST_HEAD(name)       struct list_head name = LIST_HEAD_INIT(name)

static inline void INIT_LIST_HEAD(struct list_head *h) {
    h->next = h; h->prev = h;
}

static inline void __list_add(struct list_head *new,
                              struct list_head *prev,
                              struct list_head *next) {
    next->prev = new;
    new->next  = next;
    new->prev  = prev;
    prev->next = new;
}

static inline void list_add(struct list_head *new, struct list_head *head) {
    __list_add(new, head, head->next);
}

static inline void list_add_tail(struct list_head *new, struct list_head *head) {
    __list_add(new, head->prev, head);
}

static inline void list_del(struct list_head *entry) {
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
    entry->next = entry->prev = NULL;
}

static inline int list_empty(const struct list_head *h) {
    return h->next == h;
}

#define list_entry(ptr, type, member)  container_of(ptr, type, member)

#define list_first_entry(ptr, type, member)  list_entry((ptr)->next, type, member)

#define list_first_entry_or_null(ptr, type, member) \
    (list_empty(ptr) ? NULL : list_first_entry(ptr, type, member))

#define list_next_entry(pos, member) \
    list_entry((pos)->member.next, typeof(*(pos)), member)

#define list_for_each(pos, head) \
    for (pos = (head)->next; pos != (head); pos = pos->next)

#define list_for_each_entry(pos, head, member) \
    for (pos = list_first_entry(head, typeof(*pos), member); \
         &pos->member != (head); \
         pos = list_next_entry(pos, member))

/* RCU variants are no-ops in single-threaded userspace tests. */
#define list_first_or_null_rcu(ptr, type, member)  list_first_entry_or_null(ptr, type, member)

#endif /* _LINUX_LIST_H */
