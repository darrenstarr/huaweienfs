/* SPDX-License-Identifier: GPL-2.0 */
/* Minimal userspace shim for <linux/sunrpc/metrics.h>. enfs_path.c
 * only references rpc_free_iostats; we declare it here, define the
 * stub in tests/stubs. */
#ifndef _LINUX_SUNRPC_METRICS_H
#define _LINUX_SUNRPC_METRICS_H

void rpc_free_iostats(void *stats);

#endif /* _LINUX_SUNRPC_METRICS_H */
