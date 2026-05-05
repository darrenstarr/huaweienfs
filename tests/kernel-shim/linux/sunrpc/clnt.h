/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/sunrpc/clnt.h>.
 *
 * Minimal struct rpc_clnt with just the fields enfs_roundrobin.c
 * touches. Real upstream clnt.h is 300 lines with a 30-field struct
 * and 13 transitive header includes — overkill for selection tests.
 */
#ifndef _LINUX_SUNRPC_CLNT_H
#define _LINUX_SUNRPC_CLNT_H

#include <linux/types.h>
#include <linux/sunrpc/xprtmultipath.h>

/* Minimal struct rpc_timeout — failover_time.c reads to_initval,
 * to_exponential, to_increment, to_retries, to_maxval to compute
 * the per-RPC timeout ladder. */
struct rpc_timeout {
    unsigned long to_initval;
    unsigned long to_maxval;
    unsigned long to_increment;
    unsigned int  to_retries;
    unsigned char to_exponential;
};

struct rpc_clnt {
    u32                       cl_prog;     /* RPC program number (NFS_PROGRAM) */
    u32                       cl_vers;     /* RPC program version */
    int                       cl_enfs;     /* 1 if this client uses enfs multipath */
    struct rpc_xprt_iter      cl_xpi;      /* embedded iterator */
    struct rpc_clnt          *cl_parent;   /* parent for v4 sub-clients */
    struct rpc_xprt          *cl_xprt;     /* main xprt */
    const struct rpc_timeout *cl_timeout;
    /* Extend as needed. */
};

#endif /* _LINUX_SUNRPC_CLNT_H */
