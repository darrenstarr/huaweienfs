/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_esunrpc_timer.c — unit tests for vendor/esunrpc/net/esunrpc/
 * timer.c (Van Jacobson RTT estimator, renamed from upstream sunrpc).
 *
 * The algorithm is the appendix-A SRTT/SDEV estimator from Jacobson
 * & Karels SIGCOMM '88. Pure math — perfect for thorough unit tests.
 *
 * Coverage:
 *   - esunrpc_rpc_init_rtt: timeo handling at boundaries; the 5-slot
 *     array initialised; behaviour for timeo > RPC_RTO_INIT (HZ/5)
 *     vs timeo ≤ RPC_RTO_INIT
 *   - esunrpc_rpc_update_rtt: timer==0 ignored; m<0 ignored;
 *     m==0 treated as m=1; SRTT/SDRTT update arithmetic; SDRTT
 *     lower bound at RPC_RTO_MIN
 *   - esunrpc_rpc_calc_rto: timer==0 returns rt->timeo; result
 *     formula (srtt+7)>>3 + sdrtt; capped at RPC_RTO_MAX
 *
 * This is the FIRST test target that compiles a vendor/esunrpc/
 * source file, exercising the just-set-up
 * tests/kernel-shim/esunrpc/ shim directory.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <linux/types.h>
#include <esunrpc/timer.h>

/* HZ = 1000 in our shim → RPC_RTO_INIT = 200, RPC_RTO_MIN = 100,
 * RPC_RTO_MAX = 60000. */
#define HZ 1000UL
#define RPC_RTO_INIT (HZ/5)        /* 200 */
#define RPC_RTO_MIN  (HZ/10)       /* 100 */
#define RPC_RTO_MAX  (60*HZ)       /* 60000 */

/* ============================================================ */
/* esunrpc_rpc_init_rtt                                         */
/* ============================================================ */

START_TEST(init_zero_timeo) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, 0);
    ck_assert_uint_eq(rt.timeo, 0);
    /* timeo (0) ≤ RPC_RTO_INIT, so init=0 → all srtt[i]=0 */
    for (int i = 0; i < 5; i++) {
        ck_assert_int_eq(rt.srtt[i], 0);
        ck_assert_uint_eq(rt.sdrtt[i], RPC_RTO_INIT);
        ck_assert_int_eq(rt.ntimeouts[i], 0);
    }
} END_TEST

START_TEST(init_default_timeo) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    ck_assert_uint_eq(rt.timeo, RPC_RTO_INIT);
    /* timeo == RPC_RTO_INIT → init=0 still (the if is strict >) */
    for (int i = 0; i < 5; i++)
        ck_assert_int_eq(rt.srtt[i], 0);
} END_TEST

START_TEST(init_large_timeo) {
    struct rpc_rtt rt;
    /* timeo > RPC_RTO_INIT → init = (timeo - RPC_RTO_INIT) << 3 */
    esunrpc_rpc_init_rtt(&rt, 1000);
    unsigned long expected_init = (1000 - RPC_RTO_INIT) << 3;
    ck_assert_uint_eq(rt.timeo, 1000);
    for (int i = 0; i < 5; i++) {
        ck_assert_uint_eq(rt.srtt[i], expected_init);
        ck_assert_uint_eq(rt.sdrtt[i], RPC_RTO_INIT);
    }
} END_TEST

START_TEST(init_huge_timeo) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, 60000);
    unsigned long expected_init = (60000 - RPC_RTO_INIT) << 3;
    ck_assert_uint_eq(rt.srtt[0], expected_init);
} END_TEST

/* ============================================================ */
/* esunrpc_rpc_update_rtt: timer==0 ignored                     */
/* ============================================================ */

START_TEST(update_timer_0_is_noop) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Snapshot before. */
    unsigned long s0 = rt.srtt[0], d0 = rt.sdrtt[0];
    esunrpc_rpc_update_rtt(&rt, 0, 100);
    /* Nothing should have changed. */
    ck_assert_uint_eq(rt.srtt[0], s0);
    ck_assert_uint_eq(rt.sdrtt[0], d0);
} END_TEST

START_TEST(update_negative_m_ignored) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, 1000);
    unsigned long s0 = rt.srtt[0];
    esunrpc_rpc_update_rtt(&rt, 1, -1);
    /* Negative m → early return; srtt unchanged. */
    ck_assert_uint_eq(rt.srtt[0], s0);
} END_TEST

START_TEST(update_zero_m_treated_as_one) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* timer=1 → adjusts srtt[0]/sdrtt[0]. With m=0 → m becomes 1. */
    esunrpc_rpc_update_rtt(&rt, 1, 0);
    /* srtt[0] was 0; new = 0 + (1 - 0) = 1. */
    ck_assert_uint_eq(rt.srtt[0], 1);
} END_TEST

/* ============================================================ */
/* esunrpc_rpc_update_rtt arithmetic                            */
/* ============================================================ */

/* From the algorithm:
 *   m -= srtt >> 3
 *   srtt += m
 *   if m<0: m = -m
 *   m -= sdrtt >> 2
 *   sdrtt += m
 *   if sdrtt < RPC_RTO_MIN: sdrtt = RPC_RTO_MIN
 */
static void update_simulate(unsigned long *srtt, unsigned long *sdrtt,
                            long m)
{
    if (m == 0) m = 1;
    long mm = m - ((long)*srtt >> 3);
    *srtt += mm;
    if (mm < 0) mm = -mm;
    mm -= (long)*sdrtt >> 2;
    *sdrtt += mm;
    if (*sdrtt < RPC_RTO_MIN) *sdrtt = RPC_RTO_MIN;
}

START_TEST(update_arithmetic_matches_reference) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    unsigned long ref_s = 0, ref_d = RPC_RTO_INIT;

    /* Drive through 100 measurements and assert SUT matches our
     * reference implementation step by step. */
    long samples[] = { 50, 100, 200, 150, 80, 120, 90, 110, 75, 160,
                       40, 220, 95, 145, 65, 175, 55, 130, 85, 195 };
    for (size_t i = 0; i < sizeof(samples)/sizeof(samples[0]); i++) {
        esunrpc_rpc_update_rtt(&rt, 1, samples[i]);
        update_simulate(&ref_s, &ref_d, samples[i]);
        ck_assert_msg(rt.srtt[0] == ref_s,
            "step %zu sample=%ld: SUT srtt=%lu, ref=%lu",
            i, samples[i], rt.srtt[0], ref_s);
        ck_assert_msg(rt.sdrtt[0] == ref_d,
            "step %zu sample=%ld: SUT sdrtt=%lu, ref=%lu",
            i, samples[i], rt.sdrtt[0], ref_d);
    }
} END_TEST

START_TEST(update_independent_per_timer_slot) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Update timer 1 only; other slots remain initialised. */
    esunrpc_rpc_update_rtt(&rt, 1, 1000);
    ck_assert_uint_eq(rt.srtt[1], 0);
    ck_assert_uint_eq(rt.srtt[2], 0);
    ck_assert_uint_eq(rt.sdrtt[1], RPC_RTO_INIT);
} END_TEST

START_TEST(update_sdrtt_floor_enforced) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Hammer with same value many times → sdrtt converges low. */
    for (int i = 0; i < 1000; i++)
        esunrpc_rpc_update_rtt(&rt, 1, 100);
    ck_assert_uint_ge(rt.sdrtt[0], RPC_RTO_MIN);
} END_TEST

/* ============================================================ */
/* esunrpc_rpc_calc_rto                                         */
/* ============================================================ */

START_TEST(calc_rto_timer_0_returns_timeo) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, 12345);
    ck_assert_uint_eq(esunrpc_rpc_calc_rto(&rt, 0), 12345);
} END_TEST

START_TEST(calc_rto_formula) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Initial state: srtt[0]=0, sdrtt[0]=RPC_RTO_INIT.
     * formula: ((srtt + 7) >> 3) + sdrtt = 0 + RPC_RTO_INIT */
    ck_assert_uint_eq(esunrpc_rpc_calc_rto(&rt, 1), RPC_RTO_INIT);
} END_TEST

START_TEST(calc_rto_capped_at_RPC_RTO_MAX) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Manually inflate srtt/sdrtt past the cap. */
    rt.srtt[0]  = (RPC_RTO_MAX << 3);
    rt.sdrtt[0] = RPC_RTO_MAX * 2;
    ck_assert_uint_eq(esunrpc_rpc_calc_rto(&rt, 1), RPC_RTO_MAX);
} END_TEST

START_TEST(calc_rto_just_below_cap_uncapped) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    rt.srtt[0]  = (RPC_RTO_MAX - 1000) << 3;
    rt.sdrtt[0] = 500;
    /* result = (srtt+7)>>3 + sdrtt = ~(RPC_RTO_MAX-1000) + 500
     *        = RPC_RTO_MAX - 500 — under cap. */
    unsigned long got = esunrpc_rpc_calc_rto(&rt, 1);
    ck_assert_uint_lt(got, RPC_RTO_MAX);
} END_TEST

/* Cross-product: every timer slot index. */
#define CALC_RTO_TIMER_TEST(N) \
    START_TEST(calc_rto_timer_##N##_uses_slot_##N##_minus_1) { \
        struct rpc_rtt rt; \
        esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT); \
        rt.srtt[N-1]  = 1000 << 3; \
        rt.sdrtt[N-1] = 500; \
        ck_assert_uint_eq(esunrpc_rpc_calc_rto(&rt, N), 1500); \
    } END_TEST

CALC_RTO_TIMER_TEST(1)
CALC_RTO_TIMER_TEST(2)
CALC_RTO_TIMER_TEST(3)
CALC_RTO_TIMER_TEST(4)
CALC_RTO_TIMER_TEST(5)

/* ============================================================ */
/* End-to-end: drive a sequence, then read RTO.                  */
/* ============================================================ */

START_TEST(e2e_steady_rtt_converges_to_predictable_rto) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Hammer the same RTT 200 times — SRTT converges to ~8*RTT,
     * SDRTT to RPC_RTO_MIN (the floor). RTO = (SRTT+7)>>3 + SDRTT. */
    for (int i = 0; i < 200; i++)
        esunrpc_rpc_update_rtt(&rt, 1, 100);
    /* SRTT should be close to 8*100 = 800. */
    ck_assert_uint_le(rt.srtt[0], 850);
    ck_assert_uint_ge(rt.srtt[0], 750);
    /* SDRTT should hit floor. */
    ck_assert_uint_eq(rt.sdrtt[0], RPC_RTO_MIN);
    /* RTO = (~800)/8 + 100 = ~200. */
    unsigned long rto = esunrpc_rpc_calc_rto(&rt, 1);
    ck_assert_uint_le(rto, 250);
    ck_assert_uint_ge(rto, 150);
} END_TEST

START_TEST(e2e_rising_rtt_grows_rto) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    for (int i = 0; i < 200; i++)
        esunrpc_rpc_update_rtt(&rt, 1, 100);
    unsigned long rto_before = esunrpc_rpc_calc_rto(&rt, 1);
    /* Now feed in much higher RTTs. */
    for (int i = 0; i < 50; i++)
        esunrpc_rpc_update_rtt(&rt, 1, 5000);
    unsigned long rto_after = esunrpc_rpc_calc_rto(&rt, 1);
    ck_assert_uint_gt(rto_after, rto_before);
} END_TEST

/* ============================================================ */
/* Suite plumbing.                                              */
/* ============================================================ */

static Suite *esunrpc_timer_suite(void)
{
    Suite *s = suite_create("esunrpc_timer");

    TCase *tci = tcase_create("init");
    tcase_add_test(tci, init_zero_timeo);
    tcase_add_test(tci, init_default_timeo);
    tcase_add_test(tci, init_large_timeo);
    tcase_add_test(tci, init_huge_timeo);
    suite_add_tcase(s, tci);

    TCase *tcu = tcase_create("update_branches");
    tcase_add_test(tcu, update_timer_0_is_noop);
    tcase_add_test(tcu, update_negative_m_ignored);
    tcase_add_test(tcu, update_zero_m_treated_as_one);
    suite_add_tcase(s, tcu);

    TCase *tca = tcase_create("update_arithmetic");
    tcase_add_test(tca, update_arithmetic_matches_reference);
    tcase_add_test(tca, update_independent_per_timer_slot);
    tcase_add_test(tca, update_sdrtt_floor_enforced);
    suite_add_tcase(s, tca);

    TCase *tcc = tcase_create("calc_rto");
    tcase_add_test(tcc, calc_rto_timer_0_returns_timeo);
    tcase_add_test(tcc, calc_rto_formula);
    tcase_add_test(tcc, calc_rto_capped_at_RPC_RTO_MAX);
    tcase_add_test(tcc, calc_rto_just_below_cap_uncapped);
    tcase_add_test(tcc, calc_rto_timer_1_uses_slot_1_minus_1);
    tcase_add_test(tcc, calc_rto_timer_2_uses_slot_2_minus_1);
    tcase_add_test(tcc, calc_rto_timer_3_uses_slot_3_minus_1);
    tcase_add_test(tcc, calc_rto_timer_4_uses_slot_4_minus_1);
    tcase_add_test(tcc, calc_rto_timer_5_uses_slot_5_minus_1);
    suite_add_tcase(s, tcc);

    TCase *tce = tcase_create("end_to_end");
    tcase_add_test(tce, e2e_steady_rtt_converges_to_predictable_rto);
    tcase_add_test(tce, e2e_rising_rtt_grows_rto);
    suite_add_tcase(s, tce);

    return s;
}

#define CHECK_RUNNER_SUITE  esunrpc_timer_suite
#include "check_runner.h"
