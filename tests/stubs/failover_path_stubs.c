// SPDX-License-Identifier: GPL-2.0
/*
 * failover_path_stubs.c — minimal stubs for test_failover_path.
 *
 * failover_path.c references several enfs-internal functions
 * (pm_ping_is_test_xprt_task, enfs_get_config_*, enfs_log_*) and
 * a few sunrpc helpers. This file provides no-op / sensible-default
 * stubs sufficient to exercise the policy logic.
 */
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>
#include <linux/sunrpc/xprt.h>

/* Default: this is NOT the path-detect probe task. The test for the
 * test-xprt-task short-circuit is covered by directly calling
 * failover_get_retry_policy with a mock that wouldn't have the
 * marker bit. */
bool pm_ping_is_test_xprt_task(struct rpc_task *t) { (void)t; return false; }

/* Config control surface. Defaults match production for tests
 * that don't manipulate the surface; tests that exercise specific
 * paths flip these via the externally-visible globals. */
int32_t fp_stub_path_detect_timeout = 30;
int32_t fp_stub_multipath_state     = 1;
int32_t fp_stub_native_link_status  = 1;

int32_t enfs_get_config_path_detect_timeout(void)
{ return fp_stub_path_detect_timeout; }
int32_t enfs_get_config_multipath_state(void)
{ return fp_stub_multipath_state; }
int32_t enfs_get_native_link_io_status(void)
{ return fp_stub_native_link_status; }

/* Call counters so tests can assert "this path was taken". */
unsigned int fp_call_count_rpc_restart_call    = 0;
unsigned int fp_call_count_rpc_delay           = 0;
unsigned int fp_call_count_rpc_exit            = 0;
unsigned int fp_call_count_xprt_release        = 0;
unsigned int fp_call_count_pm_set_path_state   = 0;
unsigned int fp_call_count_rpc_init_retry      = 0;
unsigned int fp_call_count_rpc_release_xprt    = 0;
int          fp_last_rpc_exit_status           = 0;
unsigned long fp_last_rpc_delay_amount         = 0;

/* Provided by enfs/sunrpc patches in production. Tests don't call
 * the retry-call paths, so a no-op return-NULL is sufficient. */
struct rpc_xprt *rpc_task_get_next_xprt(struct rpc_clnt *clnt)
{ (void)clnt; return NULL; }

struct rpc_xprt_iter;
struct rpc_xprt *xprt_iter_get_xprt(struct rpc_xprt_iter *xpi)
{ (void)xpi; return NULL; }
struct rpc_xprt *xprt_iter_get_next(struct rpc_xprt_iter *xpi)
{ (void)xpi; return NULL; }

/* Symbol used by reselect_xprt's cursor-set path. */
struct rpc_xprt;
void enfs_lb_set_cursor_xprt_iter(struct rpc_xprt_iter *xpi,
                                  struct rpc_xprt *xprt)
{ (void)xpi; (void)xprt; }

/* pm_set_path_state — needed only by failover_handle path the
 * tests don't reach. Signature must match the declaration in
 * tests/kernel-shim/enfs_preempt.h. */
enum enfs_path_state;
void pm_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state state)
{ (void)xprt; (void)state; fp_call_count_pm_set_path_state++; }

/* Diagnostic-print helpers — failover_handle prints the dead-path
 * address; tests don't read what's printed. */
void pm_get_path_state_desc(struct rpc_xprt *xprt, char *buf, int len)
{ (void)xprt; (void)buf; (void)len; }
void pm_get_xprt_state_desc(struct rpc_xprt *xprt, char *buf, int len)
{ (void)xprt; (void)buf; (void)len; }

/* pm_get_path_state — failover_prepare_transmit reads it but the
 * tests we care about (the policy decisions) never reach this code
 * path. Returns NORMAL (== eligible) so any test that does reach it
 * doesn't accidentally trigger the dead-path branch. */
enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt)
{ (void)xprt; return 1; /* PM_STATE_NORMAL */ }

/* ktime helpers used by failover_exit_return_timeout. */
typedef long long ktime_t;
unsigned int fp_stub_ktime_ms_delta = 0;  /* what ms_delta returns */

ktime_t ktime_get(void) { return 0; }
unsigned int ktime_ms_delta(ktime_t a, ktime_t b)
{ (void)a; (void)b; return fp_stub_ktime_ms_delta; }

/* Reset hook for the failover_path test to use. */
void fp_stub_reset(void)
{
    fp_stub_path_detect_timeout = 30;
    fp_stub_multipath_state     = 1;
    fp_stub_native_link_status  = 1;
    fp_stub_ktime_ms_delta      = 0;
    fp_call_count_rpc_restart_call    = 0;
    fp_call_count_rpc_delay           = 0;
    fp_call_count_rpc_exit            = 0;
    fp_call_count_xprt_release        = 0;
    fp_call_count_pm_set_path_state   = 0;
    fp_call_count_rpc_init_retry      = 0;
    fp_call_count_rpc_release_xprt    = 0;
    fp_last_rpc_exit_status           = 0;
    fp_last_rpc_delay_amount          = 0;
}

/* Reset hook called from test setup. No mutable state in this stubs
 * file; just keep the symbol so check_runner.h's runner can call it
 * if some future test needs to. */
void stub_reset_all(void) { }

/* Logging stubs for enfs_log_* macros that may resolve through
 * compat headers; no-op suffices. (printk variations are in
 * tests/stubs/kernel_stubs.c.) */
