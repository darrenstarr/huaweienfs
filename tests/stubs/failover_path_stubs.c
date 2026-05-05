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

/* Config defaults sufficient for the policy code. */
int32_t enfs_get_config_path_detect_timeout(void) { return 30; }
int32_t enfs_get_config_multipath_state(void) { return 1; }
int32_t enfs_get_native_link_io_status(void) { return 1; }

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
{ (void)xprt; (void)state; }

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

/* ktime helpers used by failover_exit_return_timeout. Tests don't
 * exercise the timeout path; safe defaults. */
typedef long long ktime_t;
ktime_t ktime_get(void) { return 0; }
unsigned int ktime_ms_delta(ktime_t a, ktime_t b) { return (unsigned int)(a - b); }

/* Reset hook called from test setup. No mutable state in this stubs
 * file; just keep the symbol so check_runner.h's runner can call it
 * if some future test needs to. */
void stub_reset_all(void) { }

/* Logging stubs for enfs_log_* macros that may resolve through
 * compat headers; no-op suffices. (printk variations are in
 * tests/stubs/kernel_stubs.c.) */
