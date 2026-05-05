/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_loadbalance_simulator.c — end-to-end load-balancing test
 *
 * Stands up a simulated multipath environment (N mock transports
 * behind one rpc_xprt_switch) and drives many "RPC dispatch" calls
 * through enfs's pure round-robin iter. Verifies the dispatcher
 * actually distributes work across paths instead of clumping on
 * one — the property we ship enfs.ko for.
 *
 * Why this exists separately from test_enfs_roundrobin.c:
 *   - That suite exercises the round-robin algorithm at the
 *     function-call level (one call, assert which xprt comes back).
 *     It catches edge cases — empty list, inactive xprt, native-link
 *     down — but says nothing about long-run distribution.
 *   - This suite asserts the *workload* property: over many
 *     dispatches, each healthy path carries roughly its share of
 *     the load. That is what a customer cares about. It is also
 *     the property that is load-bearing for the perf-tuning
 *     conclusions in chapter 12 (the per-xprt counter equality on
 *     /proc/self/mountstats v3rr line).
 *
 * Failure modes this catches that the per-call tests do not:
 *   - Cursor wraparound bug that double-counts the last xprt
 *   - Inactive-skip logic that regresses to "always pick xprt 0"
 *     if state changes mid-walk
 *   - Subtle off-by-one in the past_cur accounting from the Tier 2
 *     pure-RR rewrite (commit 8db4ee4 — see chapter 12 §12.4.1)
 *
 * What this is NOT yet:
 *   - It does not push bytes over a socket. The xprts are mock
 *     structs with counters; there is no network. The point is to
 *     validate the dispatcher's selection policy, not the transport.
 *   - It does not yet exercise the esunrpc.ko fork (PR #37). When
 *     esunrpc grows its own multipath iter, this suite gets a
 *     parallel test target. See follow-up issue.
 */
#include <check.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtmultipath.h>

#include "enfs.h"
#include "enfs_roundrobin.h"
#include "enfs_config.h"
#include "pm_state.h"

extern int32_t stub_native_link_io_status;
extern void stub_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state s);
extern void stub_reset_all(void);

struct rpc_xprt *
enfs_lb_find_next_entry_roundrobin(struct rpc_xprt_switch *xps,
                                   const struct rpc_xprt *cur);

/* ---------------------------------------------------------------- */
/* Mock-xprt + per-xprt counter scaffolding.                        */
/* ---------------------------------------------------------------- */

struct lb_counter_ctx {
    struct enfs_xprt_context base;  /* must be first — code reads .main */
    unsigned long picks;
};

static struct rpc_xprt *
lb_make_xprt(bool main, enum enfs_path_state state)
{
    struct rpc_xprt *xprt = calloc(1, sizeof(*xprt));
    ck_assert_ptr_nonnull(xprt);
    kref_init(&xprt->kref);
    INIT_LIST_HEAD(&xprt->xprt_switch);

    struct lb_counter_ctx *cctx = calloc(1, sizeof(*cctx));
    ck_assert_ptr_nonnull(cctx);
    cctx->base.main = main;
    cctx->picks = 0;
    xprt_set_reserve_context(xprt, &cctx->base);

    stub_set_path_state(xprt, state);
    return xprt;
}

static struct rpc_xprt_switch *lb_make_xps(void)
{
    struct rpc_xprt_switch *xps = calloc(1, sizeof(*xps));
    ck_assert_ptr_nonnull(xps);
    spin_lock_init(&xps->xps_lock);
    INIT_LIST_HEAD(&xps->xps_xprt_list);
    xps->xps_nxprts = 0;
    return xps;
}

static void lb_xps_add(struct rpc_xprt_switch *xps, struct rpc_xprt *x)
{
    list_add_tail(&x->xprt_switch, &xps->xps_xprt_list);
    xps->xps_nxprts++;
}

static unsigned long lb_picks(struct rpc_xprt *xprt)
{
    struct lb_counter_ctx *cctx =
        (struct lb_counter_ctx *)xprt_get_reserve_context(xprt);
    return cctx->picks;
}

/*
 * lb_dispatch_n — drive the dispatcher N times, recording each pick.
 * Cursor advances after each call, mirroring what
 * enfs_lb_set_cursor_xprt does in production via smp_load_acquire +
 * smp_store_release. Single-threaded test, so no atomics needed.
 */
static void lb_dispatch_n(struct rpc_xprt_switch *xps, unsigned int n)
{
    struct rpc_xprt *cur = NULL;
    for (unsigned int i = 0; i < n; i++) {
        struct rpc_xprt *got =
            enfs_lb_find_next_entry_roundrobin(xps, cur);
        ck_assert_ptr_nonnull(got);
        struct lb_counter_ctx *cctx =
            (struct lb_counter_ctx *)xprt_get_reserve_context(got);
        cctx->picks++;
        cur = got;
    }
}

static void setup(void)    { stub_reset_all(); }
static void teardown(void) { /* fork-isolated */ }

/* ---------------------------------------------------------------- */
/* Tests.                                                           */
/* ---------------------------------------------------------------- */

/*
 * 16 healthy xprts, 16,000 dispatches → each xprt sees exactly 1,000
 * picks. The dispatcher is deterministic, so this is exact, not
 * "approximately equal". A fairness deviation of a single pick
 * indicates a regression in the cursor-advance logic.
 */
START_TEST(sixteen_healthy_xprts_perfect_distribution)
{
    const unsigned int N = 16;
    const unsigned int DISPATCHES = 16000;

    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *xs[N];
    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(/*main*/i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }

    lb_dispatch_n(xps, DISPATCHES);

    unsigned long expected = DISPATCHES / N;
    for (unsigned int i = 0; i < N; i++) {
        ck_assert_msg(lb_picks(xs[i]) == expected,
            "xprt %u got %lu picks, expected exactly %lu (deterministic RR)",
            i, lb_picks(xs[i]), expected);
    }
}
END_TEST

/*
 * Native link down → main xprt is skipped from rotation. Remaining
 * 15 healthy xprts share the load. Verifies the
 * `nativeLinkStatus || !enfs_is_main_xprt(pos)` gate at
 * enfs_roundrobin.c line 87.
 */
START_TEST(native_link_down_skips_main)
{
    const unsigned int N = 16;
    const unsigned int ELIGIBLE = N - 1;
    const unsigned int DISPATCHES = 1500;          /* 1500 / 15 = 100 each */

    stub_native_link_io_status = 0;

    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *xs[N];
    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(/*main*/i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }

    lb_dispatch_n(xps, DISPATCHES);

    ck_assert_msg(lb_picks(xs[0]) == 0,
        "main xprt picked %lu times despite native_link_io=0",
        lb_picks(xs[0]));

    unsigned long expected = DISPATCHES / ELIGIBLE;
    for (unsigned int i = 1; i < N; i++) {
        ck_assert_msg(lb_picks(xs[i]) == expected,
            "non-main xprt %u got %lu, expected %lu",
            i, lb_picks(xs[i]), expected);
    }
}
END_TEST

/*
 * Mid-workload path failure: 16 xprts start healthy, dispatch 800
 * RPCs, mark xprt 7 as DOWN, dispatch another 1500. Expect:
 *   - xprt 7 sees PHASE_A/16 picks total and zero in the second phase
 *   - the other 15 share PHASE_B picks evenly
 *
 * This is the "single-NIC failure during sustained traffic"
 * scenario. If the dispatcher kept handing requests to xprt 7
 * after the state change, the customer's I/O would stall.
 */
START_TEST(mid_workload_path_down_diverts_traffic)
{
    const unsigned int N = 16;
    const unsigned int PHASE_A = 800;
    const unsigned int PHASE_B = 1500;
    const unsigned int DEAD = 7;

    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *xs[N];
    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(/*main*/i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }

    lb_dispatch_n(xps, PHASE_A);
    unsigned long picks_before = lb_picks(xs[DEAD]);
    ck_assert_uint_eq(picks_before, PHASE_A / N);

    stub_set_path_state(xs[DEAD], PM_STATE_FAULT);

    lb_dispatch_n(xps, PHASE_B);

    ck_assert_msg(lb_picks(xs[DEAD]) == picks_before,
        "dead xprt got %lu picks in phase B (expected 0)",
        lb_picks(xs[DEAD]) - picks_before);

    unsigned long expected_b = PHASE_B / (N - 1);
    for (unsigned int i = 0; i < N; i++) {
        if (i == DEAD)
            continue;
        unsigned long delta = lb_picks(xs[i]) - PHASE_A / N;
        ck_assert_msg(delta == expected_b,
            "xprt %u phase-B delta %lu (expected %lu)",
            i, delta, expected_b);
    }
}
END_TEST

/*
 * No healthy xprts → dispatcher returns NULL. Caller falls back to
 * the main xprt (production logic in
 * enfs_lb_switch_get_next_xprt_roundrobin); the dispatcher itself
 * just signals "nothing eligible".
 */
START_TEST(all_xprts_down_returns_null)
{
    struct rpc_xprt_switch *xps = lb_make_xps();
    for (unsigned int i = 0; i < 8; i++) {
        struct rpc_xprt *x =
            lb_make_xprt(/*main*/false, PM_STATE_FAULT);
        lb_xps_add(xps, x);
    }

    struct rpc_xprt *got =
        enfs_lb_find_next_entry_roundrobin(xps, NULL);
    ck_assert_ptr_null(got);
}
END_TEST

/*
 * Asymmetric path counts: 4 xprts, two are DOWN. Dispatcher walks
 * past dead ones and lands on the next live one. Verifies the
 * past_cur bookkeeping continues to work even when consecutive
 * xprts are ineligible.
 */
START_TEST(consecutive_inactive_xprts_are_skipped)
{
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *a = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *b = lb_make_xprt(false, PM_STATE_FAULT);
    struct rpc_xprt *c = lb_make_xprt(false, PM_STATE_FAULT);
    struct rpc_xprt *d = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, a);
    lb_xps_add(xps, b);
    lb_xps_add(xps, c);
    lb_xps_add(xps, d);

    /* From a (alive), the next live xprt past b and c is d. */
    ck_assert_ptr_eq(
        enfs_lb_find_next_entry_roundrobin(xps, a), d);
    /* From d, wrap to the head; first live xprt is a. */
    ck_assert_ptr_eq(
        enfs_lb_find_next_entry_roundrobin(xps, d), a);
}
END_TEST

/* ---------------------------------------------------------------- */
/* Suite plumbing.                                                  */
/* ---------------------------------------------------------------- */

static Suite *loadbalance_suite(void)
{
    Suite *s = suite_create("loadbalance_simulator");

    TCase *tc = tcase_create("distribution");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, sixteen_healthy_xprts_perfect_distribution);
    tcase_add_test(tc, native_link_down_skips_main);
    tcase_add_test(tc, mid_workload_path_down_diverts_traffic);
    tcase_add_test(tc, all_xprts_down_returns_null);
    tcase_add_test(tc, consecutive_inactive_xprts_are_skipped);
    suite_add_tcase(s, tc);
    return s;
}

int main(void)
{
    SRunner *sr = srunner_create(loadbalance_suite());
    srunner_set_log(sr, NULL);
    srunner_run_all(sr, CK_VERBOSE);
    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
