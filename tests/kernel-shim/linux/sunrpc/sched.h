/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <linux/sunrpc/sched.h>.
 *
 * Minimal struct rpc_task / struct rpc_message / struct rpc_procinfo
 * with just the fields tested code reads. Real upstream sched.h is
 * ~370 lines; this captures the load-bearing subset.
 */
#ifndef _LINUX_SUNRPC_SCHED_H
#define _LINUX_SUNRPC_SCHED_H

#include <linux/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprt.h>

/* Subset of RPC task flags used by tested code. */
#define RPC_TASK_ASYNC          0x0001
#define RPC_TASK_SOFT           0x0002
#define RPC_TASK_NULLCREDS      0x0010
#define RPC_TASK_FIXED          0x0020   /* failover policy honours this */
#define RPC_TASK_SENT           0x4000

/* RPC procedure descriptor — lives inside rpc_msg.rpc_proc. */
struct rpc_procinfo {
    u32                  p_proc;     /* NFSv3 wire procedure number */
    u32                  p_statidx;  /* NFSv4 stat index */
    const char          *p_name;
};

/* The kernel's rpc_message — just the proc pointer is touched here. */
struct rpc_message {
    const struct rpc_procinfo *rpc_proc;
    void                       *rpc_argp;
    void                       *rpc_resp;
};

/* rpc_task — minimal subset. Tests construct these directly. */
struct rpc_task {
    struct rpc_message   tk_msg;
    struct rpc_clnt     *tk_client;
    struct rpc_xprt     *tk_xprt;
    unsigned long        tk_flags;
    unsigned long        tk_runstate;     /* RPC_TASK_SENT bit etc. */
    unsigned long        tk_start;
    int                  tk_status;       /* per-call result */
};

/* RPC_WAS_SENT(task) — production reads RPC_TASK_SENT bit of tk_runstate
 * via test_bit; we use a simple bitmask test. Must be 0 for "not yet
 * sent" (forces FAILOVER_RETRY in failover_get_retry_policy). */
#define RPC_WAS_SENT(t) (((t)->tk_runstate & (1UL << 5)) != 0)

/* No-op placeholders for the rpc_* helpers production code calls. */
static inline void rpc_init_task_retry_counters(struct rpc_task *t) { (void)t; }
static inline int  rpc_restart_call(struct rpc_task *t) { (void)t; return 0; }
static inline void rpc_delay(struct rpc_task *t, unsigned long d) { (void)t; (void)d; }
static inline void rpc_exit(struct rpc_task *t, int e) { (void)t; (void)e; }
static inline void rpc_task_release_transport(struct rpc_task *t) { (void)t; }
static inline void xprt_release(struct rpc_task *t) { (void)t; }

/* enfs's exported helper from sunrpc patches. */
struct rpc_xprt *rpc_task_get_next_xprt(struct rpc_clnt *clnt);

#endif /* _LINUX_SUNRPC_SCHED_H */
