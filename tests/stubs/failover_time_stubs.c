// SPDX-License-Identifier: GPL-2.0
/*
 * failover_time_stubs.c — fakes for test_failover_time.
 *
 * failover_time.c reads enfs_get_config_multipath_state(),
 * enfs_get_config_multipath_timeout(), enfs_get_config_path_detect_timeout(),
 * pm_ping_is_test_xprt_task(), ktime_get/_ms_delta(), and global
 * jiffies. All are exposed here as control variables the test
 * driver can mutate per-case.
 */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>

/* === Configuration mocks ============================================ */

int  stub_multipath_state           = 1;   /* ENFS_MULTIPATH_ENABLE */
long stub_multipath_timeout_secs    = 0;   /* 0 => use clnt.cl_timeout->to_initval */
long stub_path_detect_timeout_secs  = 30;
bool stub_is_test_xprt_task         = false;

int32_t enfs_get_config_multipath_state(void) { return stub_multipath_state; }
int32_t enfs_get_config_multipath_timeout(void) { return (int32_t)stub_multipath_timeout_secs; }
int32_t enfs_get_config_path_detect_timeout(void) { return (int32_t)stub_path_detect_timeout_secs; }

bool pm_ping_is_test_xprt_task(struct rpc_task *t)
{ (void)t; return stub_is_test_xprt_task; }

/* === ktime mocks ==================================================== */
/* failover_init_task_req computes
 *    current_timeout = (ktime_ms_delta(ktime_get(), task->tk_start)) * HZ / MSEC_PER_SEC
 * Both ktime_get + ktime_ms_delta are controllable so tests can pin
 * the elapsed-time value seen by the SUT. */

typedef long long ktime_t;

ktime_t stub_ktime_now      = 0;     /* what ktime_get() returns */
unsigned int stub_ktime_ms_delta = 0; /* what ktime_ms_delta() returns */

ktime_t ktime_get(void) { return stub_ktime_now; }
unsigned int ktime_ms_delta(ktime_t a, ktime_t b)
{ (void)a; (void)b; return stub_ktime_ms_delta; }

/* === jiffies ========================================================= */
/* Both failover_init_task_req paths set req->rq_majortimeo as
 * (timeout - current_timeout) + jiffies (or just jiffies). */
unsigned long jiffies = 0;

/* === Reset hook ===================================================== */
/* Tests call this in setup so each test case starts clean. */
void stub_failover_time_reset(void)
{
    stub_multipath_state          = 1;
    stub_multipath_timeout_secs   = 0;
    stub_path_detect_timeout_secs = 30;
    stub_is_test_xprt_task        = false;
    stub_ktime_now                = 0;
    stub_ktime_ms_delta           = 0;
    jiffies                       = 0;
}

void stub_reset_all(void) { stub_failover_time_reset(); }
