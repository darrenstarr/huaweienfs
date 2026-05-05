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

/* ================================================================ */
/* Parameterised distribution tests across many N.                  */
/* ================================================================ */

/*
 * Helper that builds N healthy xprts (one main, rest non-main),
 * dispatches DISP RPCs, and asserts each xprt got exactly DISP/N.
 * The N×DISP product is the test "load"; the deterministic RR
 * iter guarantees exact equality, not approximate.
 */
static void run_perfect_distribution(unsigned int N, unsigned int DISP)
{
    ck_assert_uint_eq(DISP % N, 0);  /* test bug if not divisible */
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(N, sizeof(*xs));
    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, DISP);
    for (unsigned int i = 0; i < N; i++) {
        ck_assert_msg(lb_picks(xs[i]) == DISP / N,
            "N=%u xprt[%u] picks=%lu expected=%u",
            N, i, lb_picks(xs[i]), DISP / N);
    }
    free(xs);
}

/* Perfect-distribution at each N. */
START_TEST(perfect_dist_N_2)   { run_perfect_distribution(2,   2000); } END_TEST
START_TEST(perfect_dist_N_3)   { run_perfect_distribution(3,   3000); } END_TEST
START_TEST(perfect_dist_N_4)   { run_perfect_distribution(4,   4000); } END_TEST
START_TEST(perfect_dist_N_5)   { run_perfect_distribution(5,   5000); } END_TEST
START_TEST(perfect_dist_N_6)   { run_perfect_distribution(6,   6000); } END_TEST
START_TEST(perfect_dist_N_7)   { run_perfect_distribution(7,   7000); } END_TEST
START_TEST(perfect_dist_N_8)   { run_perfect_distribution(8,   8000); } END_TEST
START_TEST(perfect_dist_N_12)  { run_perfect_distribution(12,  12000); } END_TEST
START_TEST(perfect_dist_N_24)  { run_perfect_distribution(24,  24000); } END_TEST
START_TEST(perfect_dist_N_32)  { run_perfect_distribution(32,  32000); } END_TEST
START_TEST(perfect_dist_N_48)  { run_perfect_distribution(48,  48000); } END_TEST
START_TEST(perfect_dist_N_64)  { run_perfect_distribution(64,  64000); } END_TEST
START_TEST(perfect_dist_N_128) { run_perfect_distribution(128, 12800); } END_TEST

/* ================================================================ */
/* Failure-pattern matrix. Each test marks K of N xprts down at
 * setup time, dispatches DISP, and checks that only the (N-K)
 * healthy xprts get traffic, evenly.                               */
/* ================================================================ */

static void run_with_K_down(unsigned int N, unsigned int K,
                            const unsigned int *down_indices,
                            unsigned int DISP)
{
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(N, sizeof(*xs));
    bool *is_down = calloc(N, sizeof(*is_down));
    for (unsigned int j = 0; j < K; j++) is_down[down_indices[j]] = true;

    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(i == 0,
            is_down[i] ? PM_STATE_FAULT : PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }

    lb_dispatch_n(xps, DISP);

    unsigned int healthy = N - K;
    unsigned long expected = (healthy > 0) ? DISP / healthy : 0;
    for (unsigned int i = 0; i < N; i++) {
        if (is_down[i]) {
            ck_assert_msg(lb_picks(xs[i]) == 0,
                "N=%u K=%u xprt[%u] DOWN got picks=%lu (expected 0)",
                N, K, i, lb_picks(xs[i]));
        } else {
            ck_assert_msg(lb_picks(xs[i]) == expected,
                "N=%u K=%u xprt[%u] healthy got picks=%lu (expected %lu)",
                N, K, i, lb_picks(xs[i]), expected);
        }
    }
    free(xs); free(is_down);
}

/* Single failure at every position (N=8). 7 perms ÷ skipped main = 7. */
START_TEST(fail_pos_1_of_8) { unsigned int d[]={1}; run_with_K_down(8,1,d,4900); } END_TEST
START_TEST(fail_pos_2_of_8) { unsigned int d[]={2}; run_with_K_down(8,1,d,4900); } END_TEST
START_TEST(fail_pos_3_of_8) { unsigned int d[]={3}; run_with_K_down(8,1,d,4900); } END_TEST
START_TEST(fail_pos_4_of_8) { unsigned int d[]={4}; run_with_K_down(8,1,d,4900); } END_TEST
START_TEST(fail_pos_5_of_8) { unsigned int d[]={5}; run_with_K_down(8,1,d,4900); } END_TEST
START_TEST(fail_pos_6_of_8) { unsigned int d[]={6}; run_with_K_down(8,1,d,4900); } END_TEST
START_TEST(fail_pos_7_of_8) { unsigned int d[]={7}; run_with_K_down(8,1,d,4900); } END_TEST

/* Two failures (adjacent / non-adjacent / first+last) at N=8. */
START_TEST(fail_adj_1_2_of_8)   { unsigned int d[]={1,2}; run_with_K_down(8,2,d,6000); } END_TEST
START_TEST(fail_adj_3_4_of_8)   { unsigned int d[]={3,4}; run_with_K_down(8,2,d,6000); } END_TEST
START_TEST(fail_adj_6_7_of_8)   { unsigned int d[]={6,7}; run_with_K_down(8,2,d,6000); } END_TEST
START_TEST(fail_split_1_4_of_8) { unsigned int d[]={1,4}; run_with_K_down(8,2,d,6000); } END_TEST
START_TEST(fail_split_2_5_of_8) { unsigned int d[]={2,5}; run_with_K_down(8,2,d,6000); } END_TEST
START_TEST(fail_split_1_7_of_8) { unsigned int d[]={1,7}; run_with_K_down(8,2,d,6000); } END_TEST

/* Half down at N=16 — alternating, contiguous-front, contiguous-back. */
START_TEST(fail_alt_half_of_16) {
    unsigned int d[]={1,3,5,7,9,11,13,15};
    run_with_K_down(16, 8, d, 8000);
} END_TEST
START_TEST(fail_front_half_of_16) {
    unsigned int d[]={1,2,3,4,5,6,7};  /* skip main (idx 0) — 7 down + main excluded gives 8 healthy non-main */
    run_with_K_down(16, 7, d, 9000);
} END_TEST
START_TEST(fail_back_half_of_16) {
    unsigned int d[]={9,10,11,12,13,14,15};
    run_with_K_down(16, 7, d, 9000);
} END_TEST

/* All but one down — the lone survivor takes everything. */
START_TEST(fail_15_of_16_survivor_takes_all) {
    unsigned int d[]={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14};
    run_with_K_down(16, 15, d, 5000);
    /* Only xprt 15 should have any picks; can't easily re-check
     * here without knowing the survivor's pick count, but the
     * helper already checks the inverse: every "down" got 0,
     * every "healthy" (which is just one) got DISP/healthy = DISP. */
} END_TEST

/* ================================================================ */
/* Path-state lifecycle. Verify each non-NORMAL state is treated
 * as INELIGIBLE for dispatch.                                      */
/* ================================================================ */

START_TEST(state_NORMAL_is_eligible) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *x = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, x);
    ck_assert_ptr_eq(enfs_lb_find_next_entry_roundrobin(xps, NULL), x);
} END_TEST
START_TEST(state_FAULT_is_ineligible) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *x = lb_make_xprt(false, PM_STATE_FAULT);
    lb_xps_add(xps, x);
    ck_assert_ptr_null(enfs_lb_find_next_entry_roundrobin(xps, NULL));
} END_TEST
START_TEST(state_INIT_is_ineligible) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *x = lb_make_xprt(false, PM_STATE_INIT);
    lb_xps_add(xps, x);
    ck_assert_ptr_null(enfs_lb_find_next_entry_roundrobin(xps, NULL));
} END_TEST
START_TEST(state_UNDEFINED_is_ineligible) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *x = lb_make_xprt(false, PM_STATE_UNDEFINED);
    lb_xps_add(xps, x);
    ck_assert_ptr_null(enfs_lb_find_next_entry_roundrobin(xps, NULL));
} END_TEST

/* ================================================================ */
/* Native-link toggle at various N. Main is skipped iff native-down. */
/* ================================================================ */

static void run_native_link_main_skipped(unsigned int N, unsigned int DISP)
{
    stub_native_link_io_status = 0;
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(N, sizeof(*xs));
    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, DISP);
    ck_assert_msg(lb_picks(xs[0]) == 0,
        "N=%u: main got %lu picks despite native_link=0",
        N, lb_picks(xs[0]));
    unsigned int healthy = N - 1;
    unsigned long expected = DISP / healthy;
    for (unsigned int i = 1; i < N; i++) {
        ck_assert_msg(lb_picks(xs[i]) == expected,
            "N=%u: non-main xprt[%u]=%lu expected=%lu",
            N, i, lb_picks(xs[i]), expected);
    }
    free(xs);
}
START_TEST(native_down_N_2)  { run_native_link_main_skipped(2,  1000); } END_TEST
START_TEST(native_down_N_4)  { run_native_link_main_skipped(4,  3000); } END_TEST
START_TEST(native_down_N_8)  { run_native_link_main_skipped(8,  7000); } END_TEST
START_TEST(native_down_N_16) { run_native_link_main_skipped(16, 15000); } END_TEST
START_TEST(native_down_N_32) { run_native_link_main_skipped(32, 31000); } END_TEST

/* Native-up + main present = main IS in rotation. */
static void run_native_up_main_in_rotation(unsigned int N, unsigned int DISP)
{
    stub_native_link_io_status = 1;
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(N, sizeof(*xs));
    for (unsigned int i = 0; i < N; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, DISP);
    unsigned long expected = DISP / N;
    for (unsigned int i = 0; i < N; i++) {
        ck_assert_msg(lb_picks(xs[i]) == expected,
            "N=%u: xprt[%u]=%lu expected=%lu (main=%s)",
            N, i, lb_picks(xs[i]), expected, i == 0 ? "yes" : "no");
    }
    free(xs);
}
START_TEST(native_up_N_2)  { run_native_up_main_in_rotation(2,  2000); } END_TEST
START_TEST(native_up_N_8)  { run_native_up_main_in_rotation(8,  8000); } END_TEST
START_TEST(native_up_N_16) { run_native_up_main_in_rotation(16, 16000); } END_TEST

/* ================================================================ */
/* Cursor placement edge cases.                                     */
/* ================================================================ */

START_TEST(cursor_at_first_advances_to_second) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *a = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *b = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *c = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, a); lb_xps_add(xps, b); lb_xps_add(xps, c);
    ck_assert_ptr_eq(enfs_lb_find_next_entry_roundrobin(xps, a), b);
} END_TEST
START_TEST(cursor_at_middle_advances_to_next) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *a = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *b = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *c = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, a); lb_xps_add(xps, b); lb_xps_add(xps, c);
    ck_assert_ptr_eq(enfs_lb_find_next_entry_roundrobin(xps, b), c);
} END_TEST
START_TEST(cursor_at_last_wraps_to_head) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *a = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *b = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *c = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, a); lb_xps_add(xps, b); lb_xps_add(xps, c);
    ck_assert_ptr_eq(enfs_lb_find_next_entry_roundrobin(xps, c), a);
} END_TEST
START_TEST(cursor_NULL_returns_first) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *a = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *b = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, a); lb_xps_add(xps, b);
    ck_assert_ptr_eq(enfs_lb_find_next_entry_roundrobin(xps, NULL), a);
} END_TEST
START_TEST(cursor_at_dead_xprt_advances_past) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt *a = lb_make_xprt(false, PM_STATE_NORMAL);
    struct rpc_xprt *dead = lb_make_xprt(false, PM_STATE_FAULT);
    struct rpc_xprt *c = lb_make_xprt(false, PM_STATE_NORMAL);
    lb_xps_add(xps, a); lb_xps_add(xps, dead); lb_xps_add(xps, c);
    /* Cursor at the dead one — next live is c, NOT dead itself. */
    ck_assert_ptr_eq(enfs_lb_find_next_entry_roundrobin(xps, dead), c);
} END_TEST

/* ================================================================ */
/* Workload-size variations at fixed N.                             */
/* ================================================================ */

START_TEST(small_workload_10_at_N_5)    { run_perfect_distribution(5,    10); } END_TEST
START_TEST(medium_workload_1k_at_N_5)   { run_perfect_distribution(5,  1000); } END_TEST
START_TEST(large_workload_100k_at_N_5)  { run_perfect_distribution(5,100000); } END_TEST

/* ============================================================ */
/* More N values for perfect distribution.                       */
/* ============================================================ */

START_TEST(perfect_dist_N_9)   { run_perfect_distribution(9,    9000); } END_TEST
START_TEST(perfect_dist_N_10)  { run_perfect_distribution(10,  10000); } END_TEST
START_TEST(perfect_dist_N_14)  { run_perfect_distribution(14,  14000); } END_TEST
START_TEST(perfect_dist_N_18)  { run_perfect_distribution(18,  18000); } END_TEST
START_TEST(perfect_dist_N_20)  { run_perfect_distribution(20,  20000); } END_TEST
START_TEST(perfect_dist_N_22)  { run_perfect_distribution(22,  22000); } END_TEST
START_TEST(perfect_dist_N_26)  { run_perfect_distribution(26,  26000); } END_TEST
START_TEST(perfect_dist_N_36)  { run_perfect_distribution(36,  36000); } END_TEST
START_TEST(perfect_dist_N_40)  { run_perfect_distribution(40,  40000); } END_TEST
START_TEST(perfect_dist_N_50)  { run_perfect_distribution(50,  50000); } END_TEST
START_TEST(perfect_dist_N_56)  { run_perfect_distribution(56,  56000); } END_TEST
START_TEST(perfect_dist_N_72)  { run_perfect_distribution(72,  72000); } END_TEST
START_TEST(perfect_dist_N_96)  { run_perfect_distribution(96,  96000); } END_TEST
START_TEST(perfect_dist_N_100) { run_perfect_distribution(100,10000); } END_TEST
START_TEST(perfect_dist_N_200) { run_perfect_distribution(200,10000); } END_TEST
START_TEST(perfect_dist_N_256) { run_perfect_distribution(256,12800); } END_TEST

/* ============================================================ */
/* More single-failure positions at N=16.                       */
/* ============================================================ */

#define FAIL_POS_16(name, idx) \
    START_TEST(name) { unsigned int d[]={idx}; run_with_K_down(16,1,d, 15000); } END_TEST

FAIL_POS_16(fail_pos_1_of_16,  1)
FAIL_POS_16(fail_pos_2_of_16,  2)
FAIL_POS_16(fail_pos_3_of_16,  3)
FAIL_POS_16(fail_pos_4_of_16,  4)
FAIL_POS_16(fail_pos_5_of_16,  5)
FAIL_POS_16(fail_pos_6_of_16,  6)
FAIL_POS_16(fail_pos_7_of_16,  7)
FAIL_POS_16(fail_pos_8_of_16,  8)
FAIL_POS_16(fail_pos_9_of_16,  9)
FAIL_POS_16(fail_pos_10_of_16, 10)
FAIL_POS_16(fail_pos_11_of_16, 11)
FAIL_POS_16(fail_pos_12_of_16, 12)
FAIL_POS_16(fail_pos_13_of_16, 13)
FAIL_POS_16(fail_pos_14_of_16, 14)
FAIL_POS_16(fail_pos_15_of_16, 15)

/* ============================================================ */
/* Many N values for native-down behavior.                       */
/* ============================================================ */

START_TEST(native_down_N_3)   { run_native_link_main_skipped(3,   2000); } END_TEST
START_TEST(native_down_N_5)   { run_native_link_main_skipped(5,   4000); } END_TEST
START_TEST(native_down_N_6)   { run_native_link_main_skipped(6,   5000); } END_TEST
START_TEST(native_down_N_10)  { run_native_link_main_skipped(10,  9000); } END_TEST
START_TEST(native_down_N_12)  { run_native_link_main_skipped(12, 11000); } END_TEST
START_TEST(native_down_N_24)  { run_native_link_main_skipped(24, 23000); } END_TEST
START_TEST(native_down_N_48)  { run_native_link_main_skipped(48, 47000); } END_TEST
START_TEST(native_down_N_64)  { run_native_link_main_skipped(64, 63000); } END_TEST

/* ================================================================ */
/* Two-failure patterns at N=16. DISP=14000 → 14000/14 = 1000. */
/* ================================================================ */

#define FAIL2_16(name, a, b) \
    START_TEST(name) { \
        unsigned int d[]={(a),(b)}; \
        run_with_K_down(16, 2, d, 14000); \
    } END_TEST

FAIL2_16(fail2_1_2_of_16,    1,  2)
FAIL2_16(fail2_2_3_of_16,    2,  3)
FAIL2_16(fail2_3_4_of_16,    3,  4)
FAIL2_16(fail2_7_8_of_16,    7,  8)
FAIL2_16(fail2_14_15_of_16, 14, 15)
FAIL2_16(fail2_1_15_of_16,   1, 15)
FAIL2_16(fail2_2_8_of_16,    2,  8)
FAIL2_16(fail2_3_11_of_16,   3, 11)
FAIL2_16(fail2_5_10_of_16,   5, 10)
FAIL2_16(fail2_6_9_of_16,    6,  9)
FAIL2_16(fail2_4_12_of_16,   4, 12)
FAIL2_16(fail2_2_15_of_16,   2, 15)

/* ================================================================ */
/* Three-failure patterns at N=16. DISP=9100 → 9100/13 = 700. */
/* ================================================================ */

#define FAIL3_16(name, a, b, c) \
    START_TEST(name) { \
        unsigned int d[]={(a),(b),(c)}; \
        run_with_K_down(16, 3, d, 9100); \
    } END_TEST

FAIL3_16(fail3_1_2_3_of_16,     1,  2,  3)
FAIL3_16(fail3_4_5_6_of_16,     4,  5,  6)
FAIL3_16(fail3_13_14_15_of_16, 13, 14, 15)
FAIL3_16(fail3_1_8_15_of_16,    1,  8, 15)
FAIL3_16(fail3_2_4_6_of_16,     2,  4,  6)
FAIL3_16(fail3_3_7_11_of_16,    3,  7, 11)
FAIL3_16(fail3_2_3_15_of_16,    2,  3, 15)
FAIL3_16(fail3_1_15_14_of_16,   1, 15, 14)

/* ================================================================ */
/* Failure-density tests: half down, three-quarters down, just one  */
/* survivor. */
/* ================================================================ */

START_TEST(density_half_N16) {
    /* 8 down, 8 up. DISP=8000 → 8000/8 = 1000 each. Indices 1,3,5,...15. */
    unsigned int d[] = {1, 3, 5, 7, 9, 11, 13, 15};
    run_with_K_down(16, 8, d, 8000);
} END_TEST

START_TEST(density_half_N32) {
    /* 16 down, 16 up. Even indices down (skip 0 = main). */
    unsigned int d[16];
    for (int i = 0; i < 16; i++) d[i] = (unsigned int)((i + 1) * 2 - 1);
    run_with_K_down(32, 16, d, 16000);
} END_TEST

START_TEST(density_threequarters_N16) {
    /* 12 down, 4 up. DISP=4000 → 4000/4 = 1000. */
    unsigned int d[] = {1, 2, 3, 5, 6, 7, 9, 10, 11, 13, 14, 15};
    run_with_K_down(16, 12, d, 4000);
} END_TEST

START_TEST(density_only_one_healthy_N16) {
    /* 15 down. Only main (idx 0) is up → all 5000 calls land on main. */
    unsigned int d[15];
    for (int i = 0; i < 15; i++) d[i] = (unsigned int)(i + 1);
    run_with_K_down(16, 15, d, 5000);
} END_TEST

START_TEST(density_only_one_healthy_N32) {
    unsigned int d[31];
    for (int i = 0; i < 31; i++) d[i] = (unsigned int)(i + 1);
    run_with_K_down(32, 31, d, 5000);
} END_TEST

/* ================================================================ */
/* Huge-N perfect distribution. 300, 400, 500, 512.                 */
/* ================================================================ */

START_TEST(perfect_dist_N_300) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(300, sizeof(*xs));
    for (int i = 0; i < 300; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, 300 * 10);
    for (int i = 0; i < 300; i++)
        ck_assert_uint_eq(lb_picks(xs[i]), 10);
    free(xs);
} END_TEST

START_TEST(perfect_dist_N_400) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(400, sizeof(*xs));
    for (int i = 0; i < 400; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, 400 * 5);
    for (int i = 0; i < 400; i++)
        ck_assert_uint_eq(lb_picks(xs[i]), 5);
    free(xs);
} END_TEST

START_TEST(perfect_dist_N_500) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(500, sizeof(*xs));
    for (int i = 0; i < 500; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, 500 * 4);
    for (int i = 0; i < 500; i++)
        ck_assert_uint_eq(lb_picks(xs[i]), 4);
    free(xs);
} END_TEST

START_TEST(perfect_dist_N_512) {
    struct rpc_xprt_switch *xps = lb_make_xps();
    struct rpc_xprt **xs = calloc(512, sizeof(*xs));
    for (int i = 0; i < 512; i++) {
        xs[i] = lb_make_xprt(i == 0, PM_STATE_NORMAL);
        lb_xps_add(xps, xs[i]);
    }
    lb_dispatch_n(xps, 512 * 4);
    for (int i = 0; i < 512; i++)
        ck_assert_uint_eq(lb_picks(xs[i]), 4);
    free(xs);
} END_TEST

/* ================================================================ */
/* Suite plumbing.                                                  */
/* ================================================================ */

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

    /* Parameterised perfect-distribution at every N value. */
    TCase *tc_n = tcase_create("perfect_distribution_by_N");
    tcase_add_checked_fixture(tc_n, setup, teardown);
    tcase_add_test(tc_n, perfect_dist_N_2);
    tcase_add_test(tc_n, perfect_dist_N_3);
    tcase_add_test(tc_n, perfect_dist_N_4);
    tcase_add_test(tc_n, perfect_dist_N_5);
    tcase_add_test(tc_n, perfect_dist_N_6);
    tcase_add_test(tc_n, perfect_dist_N_7);
    tcase_add_test(tc_n, perfect_dist_N_8);
    tcase_add_test(tc_n, perfect_dist_N_12);
    tcase_add_test(tc_n, perfect_dist_N_24);
    tcase_add_test(tc_n, perfect_dist_N_32);
    tcase_add_test(tc_n, perfect_dist_N_48);
    tcase_add_test(tc_n, perfect_dist_N_64);
    tcase_add_test(tc_n, perfect_dist_N_128);
    suite_add_tcase(s, tc_n);

    /* Single + multi failures at N=8 and N=16. */
    TCase *tc_f = tcase_create("failure_patterns");
    tcase_add_checked_fixture(tc_f, setup, teardown);
    tcase_add_test(tc_f, fail_pos_1_of_8);
    tcase_add_test(tc_f, fail_pos_2_of_8);
    tcase_add_test(tc_f, fail_pos_3_of_8);
    tcase_add_test(tc_f, fail_pos_4_of_8);
    tcase_add_test(tc_f, fail_pos_5_of_8);
    tcase_add_test(tc_f, fail_pos_6_of_8);
    tcase_add_test(tc_f, fail_pos_7_of_8);
    tcase_add_test(tc_f, fail_adj_1_2_of_8);
    tcase_add_test(tc_f, fail_adj_3_4_of_8);
    tcase_add_test(tc_f, fail_adj_6_7_of_8);
    tcase_add_test(tc_f, fail_split_1_4_of_8);
    tcase_add_test(tc_f, fail_split_2_5_of_8);
    tcase_add_test(tc_f, fail_split_1_7_of_8);
    tcase_add_test(tc_f, fail_alt_half_of_16);
    tcase_add_test(tc_f, fail_front_half_of_16);
    tcase_add_test(tc_f, fail_back_half_of_16);
    tcase_add_test(tc_f, fail_15_of_16_survivor_takes_all);
    suite_add_tcase(s, tc_f);

    /* Path-state lifecycle. */
    TCase *tc_st = tcase_create("path_state_eligibility");
    tcase_add_checked_fixture(tc_st, setup, teardown);
    tcase_add_test(tc_st, state_NORMAL_is_eligible);
    tcase_add_test(tc_st, state_FAULT_is_ineligible);
    tcase_add_test(tc_st, state_INIT_is_ineligible);
    tcase_add_test(tc_st, state_UNDEFINED_is_ineligible);
    suite_add_tcase(s, tc_st);

    /* Native-link toggle. */
    TCase *tc_nl = tcase_create("native_link");
    tcase_add_checked_fixture(tc_nl, setup, teardown);
    tcase_add_test(tc_nl, native_down_N_2);
    tcase_add_test(tc_nl, native_down_N_4);
    tcase_add_test(tc_nl, native_down_N_8);
    tcase_add_test(tc_nl, native_down_N_16);
    tcase_add_test(tc_nl, native_down_N_32);
    tcase_add_test(tc_nl, native_up_N_2);
    tcase_add_test(tc_nl, native_up_N_8);
    tcase_add_test(tc_nl, native_up_N_16);
    suite_add_tcase(s, tc_nl);

    /* Cursor edge cases. */
    TCase *tc_cur = tcase_create("cursor_placement");
    tcase_add_checked_fixture(tc_cur, setup, teardown);
    tcase_add_test(tc_cur, cursor_at_first_advances_to_second);
    tcase_add_test(tc_cur, cursor_at_middle_advances_to_next);
    tcase_add_test(tc_cur, cursor_at_last_wraps_to_head);
    tcase_add_test(tc_cur, cursor_NULL_returns_first);
    tcase_add_test(tc_cur, cursor_at_dead_xprt_advances_past);
    suite_add_tcase(s, tc_cur);

    /* Workload-size variations. */
    TCase *tc_w = tcase_create("workload_sizes");
    tcase_add_checked_fixture(tc_w, setup, teardown);
    tcase_add_test(tc_w, small_workload_10_at_N_5);
    tcase_add_test(tc_w, medium_workload_1k_at_N_5);
    tcase_add_test(tc_w, large_workload_100k_at_N_5);
    suite_add_tcase(s, tc_w);

    /* More N values for perfect distribution. */
    TCase *tc_n2 = tcase_create("perfect_distribution_more_N");
    tcase_add_checked_fixture(tc_n2, setup, teardown);
    tcase_add_test(tc_n2, perfect_dist_N_9);
    tcase_add_test(tc_n2, perfect_dist_N_10);
    tcase_add_test(tc_n2, perfect_dist_N_14);
    tcase_add_test(tc_n2, perfect_dist_N_18);
    tcase_add_test(tc_n2, perfect_dist_N_20);
    tcase_add_test(tc_n2, perfect_dist_N_22);
    tcase_add_test(tc_n2, perfect_dist_N_26);
    tcase_add_test(tc_n2, perfect_dist_N_36);
    tcase_add_test(tc_n2, perfect_dist_N_40);
    tcase_add_test(tc_n2, perfect_dist_N_50);
    tcase_add_test(tc_n2, perfect_dist_N_56);
    tcase_add_test(tc_n2, perfect_dist_N_72);
    tcase_add_test(tc_n2, perfect_dist_N_96);
    tcase_add_test(tc_n2, perfect_dist_N_100);
    tcase_add_test(tc_n2, perfect_dist_N_200);
    tcase_add_test(tc_n2, perfect_dist_N_256);
    suite_add_tcase(s, tc_n2);

    /* All single-failure positions at N=16. */
    TCase *tc_f16 = tcase_create("single_failure_at_every_position_N16");
    tcase_add_checked_fixture(tc_f16, setup, teardown);
    tcase_add_test(tc_f16, fail_pos_1_of_16);
    tcase_add_test(tc_f16, fail_pos_2_of_16);
    tcase_add_test(tc_f16, fail_pos_3_of_16);
    tcase_add_test(tc_f16, fail_pos_4_of_16);
    tcase_add_test(tc_f16, fail_pos_5_of_16);
    tcase_add_test(tc_f16, fail_pos_6_of_16);
    tcase_add_test(tc_f16, fail_pos_7_of_16);
    tcase_add_test(tc_f16, fail_pos_8_of_16);
    tcase_add_test(tc_f16, fail_pos_9_of_16);
    tcase_add_test(tc_f16, fail_pos_10_of_16);
    tcase_add_test(tc_f16, fail_pos_11_of_16);
    tcase_add_test(tc_f16, fail_pos_12_of_16);
    tcase_add_test(tc_f16, fail_pos_13_of_16);
    tcase_add_test(tc_f16, fail_pos_14_of_16);
    tcase_add_test(tc_f16, fail_pos_15_of_16);
    suite_add_tcase(s, tc_f16);

    /* Native-link-down at more N values. */
    TCase *tc_nd2 = tcase_create("native_link_down_more_N");
    tcase_add_checked_fixture(tc_nd2, setup, teardown);
    tcase_add_test(tc_nd2, native_down_N_3);
    tcase_add_test(tc_nd2, native_down_N_5);
    tcase_add_test(tc_nd2, native_down_N_6);
    tcase_add_test(tc_nd2, native_down_N_10);
    tcase_add_test(tc_nd2, native_down_N_12);
    tcase_add_test(tc_nd2, native_down_N_24);
    tcase_add_test(tc_nd2, native_down_N_48);
    tcase_add_test(tc_nd2, native_down_N_64);
    suite_add_tcase(s, tc_nd2);

    /* Two-failure patterns at N=16: every adjacent + non-adjacent
     * combo over a representative sample. DISP picked so DISP /
     * (16-2) = clean integer to avoid bucket-rounding noise. */
    TCase *tc_f2_16 = tcase_create("two_failures_at_N16");
    tcase_add_checked_fixture(tc_f2_16, setup, teardown);
    tcase_add_test(tc_f2_16, fail2_1_2_of_16);
    tcase_add_test(tc_f2_16, fail2_2_3_of_16);
    tcase_add_test(tc_f2_16, fail2_3_4_of_16);
    tcase_add_test(tc_f2_16, fail2_7_8_of_16);
    tcase_add_test(tc_f2_16, fail2_14_15_of_16);
    tcase_add_test(tc_f2_16, fail2_1_15_of_16);
    tcase_add_test(tc_f2_16, fail2_2_8_of_16);
    tcase_add_test(tc_f2_16, fail2_3_11_of_16);
    tcase_add_test(tc_f2_16, fail2_5_10_of_16);
    tcase_add_test(tc_f2_16, fail2_6_9_of_16);
    tcase_add_test(tc_f2_16, fail2_4_12_of_16);
    tcase_add_test(tc_f2_16, fail2_2_15_of_16);
    suite_add_tcase(s, tc_f2_16);

    /* Three-failure patterns at N=16. DISP = 9100 → 9100/13 = 700. */
    TCase *tc_f3_16 = tcase_create("three_failures_at_N16");
    tcase_add_checked_fixture(tc_f3_16, setup, teardown);
    tcase_add_test(tc_f3_16, fail3_1_2_3_of_16);
    tcase_add_test(tc_f3_16, fail3_4_5_6_of_16);
    tcase_add_test(tc_f3_16, fail3_13_14_15_of_16);
    tcase_add_test(tc_f3_16, fail3_1_8_15_of_16);
    tcase_add_test(tc_f3_16, fail3_2_4_6_of_16);
    tcase_add_test(tc_f3_16, fail3_3_7_11_of_16);
    tcase_add_test(tc_f3_16, fail3_2_3_15_of_16);
    tcase_add_test(tc_f3_16, fail3_1_15_14_of_16);
    suite_add_tcase(s, tc_f3_16);

    /* Half-down and three-quarters-down density tests. */
    TCase *tc_density = tcase_create("failure_density");
    tcase_add_checked_fixture(tc_density, setup, teardown);
    tcase_add_test(tc_density, density_half_N16);
    tcase_add_test(tc_density, density_half_N32);
    tcase_add_test(tc_density, density_threequarters_N16);
    tcase_add_test(tc_density, density_only_one_healthy_N16);
    tcase_add_test(tc_density, density_only_one_healthy_N32);
    suite_add_tcase(s, tc_density);

    /* Larger-N perfect distribution beyond the existing tests. */
    TCase *tc_n3 = tcase_create("perfect_distribution_huge_N");
    tcase_add_checked_fixture(tc_n3, setup, teardown);
    tcase_add_test(tc_n3, perfect_dist_N_300);
    tcase_add_test(tc_n3, perfect_dist_N_400);
    tcase_add_test(tc_n3, perfect_dist_N_500);
    tcase_add_test(tc_n3, perfect_dist_N_512);
    suite_add_tcase(s, tc_n3);

    return s;
}

#define CHECK_RUNNER_SUITE  loadbalance_suite
#include "check_runner.h"
