/* SPDX-License-Identifier: GPL-2.0 */
/* Userspace shim for <esunrpc/xprt.h>. Minimal struct rpc_xprt with
 * just the fields tested code reads. Real upstream xprt.h is ~500
 * lines + drags in <linux/ktime.h> and many more. */
#ifndef _ESUNRPC_XPRT_H
#define _ESUNRPC_XPRT_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/socket.h>

/* Subset of XPRT_* state bits used by tested code. */
#define XPRT_LOCKED         0
#define XPRT_CONNECTED      1
#define XPRT_BINDING        2
#define XPRT_CONNECTING     3
#define XPRT_BOUND          4
#define XPRT_CLOSE_WAIT     5
#define XPRT_CONGESTED      6
#define XPRT_OFFLINE        7
#define XPRT_REMOVE         8

struct rpc_xprt {
    struct kref               kref;
    struct list_head          xprt_switch;     /* in xps_xprt_list */
    unsigned long             state;            /* XPRT_* bits */
    struct sockaddr_storage   addr;
    struct sockaddr_storage   srcaddr;
    int                       addrlen;
};

struct rpc_xprt *xprt_get(struct rpc_xprt *x);
void xprt_put(struct rpc_xprt *x);

#endif /* _ESUNRPC_XPRT_H */
