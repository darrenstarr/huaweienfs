/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Userspace shim for <esunrpc/timer.h>. Just struct rpc_rtt and the
 * three function decls — sufficient to compile esunrpc/timer.c
 * against in userspace.
 */
#ifndef _ESUNRPC_TIMER_H
#define _ESUNRPC_TIMER_H

#include <linux/atomic.h>

struct rpc_rtt {
    unsigned long timeo;       /* default timeout value */
    unsigned long srtt[5];     /* smoothed round trip time << 3 */
    unsigned long sdrtt[5];    /* smoothed medium deviation of RTT */
    int           ntimeouts[5];
};

extern void esunrpc_rpc_init_rtt(struct rpc_rtt *rt, unsigned long timeo);
extern void esunrpc_rpc_update_rtt(struct rpc_rtt *rt, unsigned timer, long m);
extern unsigned long esunrpc_rpc_calc_rto(struct rpc_rtt *rt, unsigned timer);

#endif /* _ESUNRPC_TIMER_H */
