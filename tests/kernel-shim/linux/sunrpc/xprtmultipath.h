/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/sunrpc/xprtmultipath.h>.
 *
 * struct rpc_xprt_switch + iterator types matching the kernel API
 * surface used by enfs_roundrobin.c. Other multipath-management
 * functions (rpc_xprt_switch_*) are declared here and stubbed in
 * tests/stubs/enfs_deps_stubs.c so links resolve.
 */
#ifndef _LINUX_SUNRPC_XPRTMULTIPATH_H
#define _LINUX_SUNRPC_XPRTMULTIPATH_H

#include <linux/types.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/sunrpc/xprt.h>

struct rpc_xprt_iter_ops;

struct rpc_xprt_switch {
    spinlock_t                       xps_lock;
    unsigned int                     xps_nxprts;       /* read by selection logic */
    unsigned int                     xps_nactive;
    struct list_head                 xps_xprt_list;    /* the chain of rpc_xprts */
    const struct rpc_xprt_iter_ops  *xps_iter_ops;     /* read/written by enfs */
    /* Extend as needed. */
};

struct rpc_xprt_iter {
    struct rpc_xprt_switch *xpi_xpswitch;
    struct rpc_xprt        *xpi_cursor;
    /* xpi_ops is in real upstream but enfs_roundrobin.c never reads it */
};

struct rpc_xprt_iter_ops {
    void              (*xpi_rewind)(struct rpc_xprt_iter *);
    struct rpc_xprt  *(*xpi_xprt)(struct rpc_xprt_iter *);
    struct rpc_xprt  *(*xpi_next)(struct rpc_xprt_iter *);
};

/* Declared so revert-policy and similar paths link. Implemented in
 * tests/stubs/enfs_deps_stubs.c — no behavior, just records the call
 * so tests can assert on it. */
void rpc_xprt_switch_set_singular(struct rpc_xprt_switch *xps);
void rpc_xprt_switch_set_roundrobin(struct rpc_xprt_switch *xps);

#endif /* _LINUX_SUNRPC_XPRTMULTIPATH_H */
