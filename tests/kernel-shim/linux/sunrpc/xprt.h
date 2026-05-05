/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/sunrpc/xprt.h>.
 *
 * Minimal struct rpc_xprt with just the fields enfs_roundrobin.c
 * accesses (kref + xprt_switch list linkage). Real upstream xprt.h
 * has 100+ fields and pulls in linux/socket.h, linux/in.h, etc. —
 * none needed for selection-logic tests.
 *
 * If/when a test needs the real layout, switch to the upstream
 * header by appending include/linux/sunrpc/xprt.h to
 * tests/LINUX-HEADERS-MANIFEST and stripping this shim down.
 */
#ifndef _LINUX_SUNRPC_XPRT_H
#define _LINUX_SUNRPC_XPRT_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/kref.h>

/* Subset of XPRT_* state bits used by enfs source under test. */
#define XPRT_LOCKED         0
#define XPRT_CONNECTED      1
#define XPRT_BOUND          4
#define XPRT_BINDING        2
#define XPRT_CONNECTING     3
#define XPRT_CLOSE_WAIT     5
#define XPRT_CONGESTED      6
#define XPRT_OFFLINE        7
#define XPRT_CLOSING        8
#define XPRT_BC_PA_IN_USE   9

struct rpc_xprt {
    struct kref          kref;        /* read by enfs_xprt_is_active() */
    struct list_head     xprt_switch; /* linkage in xps->xps_xprt_list */
    unsigned long        state;        /* used by pm_state.c xprt-state-desc */
    struct sockaddr_storage addr;     /* peer addr, used by pm_state diagnostics */
    /* Tests can extend by adding fields here as new code-under-test
     * needs them. Anything used must be a real field with the right
     * type. */
};

/* test_bit / __set_bit / __clear_bit shims for the XPRT state bitmask.
 * Real kernel uses bitops/atomic; tests don't need atomicity. */
static inline int test_bit(unsigned long bit, const unsigned long *addr)
{
    return (*addr & (1UL << bit)) != 0;
}
static inline void __set_bit(unsigned long bit, unsigned long *addr)
{
    *addr |= (1UL << bit);
}
static inline void __clear_bit(unsigned long bit, unsigned long *addr)
{
    *addr &= ~(1UL << bit);
}

/* xprt_get / xprt_put: production refcounts; tests just need them
 * to compile and not segfault. Stubbed as no-ops here. */
static inline struct rpc_xprt *xprt_get(struct rpc_xprt *x) { return x; }
static inline void xprt_put(struct rpc_xprt *x) { (void)x; }

#endif /* _LINUX_SUNRPC_XPRT_H */
