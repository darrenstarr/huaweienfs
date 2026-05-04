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

struct rpc_xprt {
    struct kref       kref;       /* read by enfs_xprt_is_active() */
    struct list_head  xprt_switch; /* linkage in xps->xps_xprt_list */
    /* Tests can extend by adding fields here as new code-under-test
     * needs them. Anything used must be a real field with the right
     * type. */
};

#endif /* _LINUX_SUNRPC_XPRT_H */
