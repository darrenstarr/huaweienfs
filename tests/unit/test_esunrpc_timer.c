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
/* Parameterised init_rtt: exercise many timeo values            */
/* ============================================================ */

#define INIT_TIMEO_TEST(name, tv) \
    START_TEST(name) { \
        struct rpc_rtt rt; \
        esunrpc_rpc_init_rtt(&rt, (tv)); \
        ck_assert_uint_eq(rt.timeo, (tv)); \
        unsigned long expected_init = \
            ((tv) > RPC_RTO_INIT) ? (((tv) - RPC_RTO_INIT) << 3) : 0; \
        for (int i = 0; i < 5; i++) { \
            ck_assert_uint_eq(rt.srtt[i], expected_init); \
            ck_assert_uint_eq(rt.sdrtt[i], RPC_RTO_INIT); \
            ck_assert_int_eq(rt.ntimeouts[i], 0); \
        } \
    } END_TEST

INIT_TIMEO_TEST(init_timeo_50,    50)
INIT_TIMEO_TEST(init_timeo_100,   100)
INIT_TIMEO_TEST(init_timeo_150,   150)
INIT_TIMEO_TEST(init_timeo_200,   200)
INIT_TIMEO_TEST(init_timeo_300,   300)
INIT_TIMEO_TEST(init_timeo_500,   500)
INIT_TIMEO_TEST(init_timeo_750,   750)
INIT_TIMEO_TEST(init_timeo_1500,  1500)
INIT_TIMEO_TEST(init_timeo_3000,  3000)
INIT_TIMEO_TEST(init_timeo_6000,  6000)
INIT_TIMEO_TEST(init_timeo_15000, 15000)
INIT_TIMEO_TEST(init_timeo_30000, 30000)
INIT_TIMEO_TEST(init_timeo_60000, 60000)

/* ============================================================ */
/* Parameterised RTT update: many sample values                 */
/* ============================================================ */

#define UPDATE_VALUE_TEST(name, value) \
    START_TEST(name) { \
        struct rpc_rtt rt; \
        esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT); \
        unsigned long ref_s = 0, ref_d = RPC_RTO_INIT; \
        update_simulate(&ref_s, &ref_d, value); \
        esunrpc_rpc_update_rtt(&rt, 1, value); \
        ck_assert_uint_eq(rt.srtt[0], ref_s); \
        ck_assert_uint_eq(rt.sdrtt[0], ref_d); \
    } END_TEST

UPDATE_VALUE_TEST(update_v_1,    1)
UPDATE_VALUE_TEST(update_v_2,    2)
UPDATE_VALUE_TEST(update_v_5,    5)
UPDATE_VALUE_TEST(update_v_10,   10)
UPDATE_VALUE_TEST(update_v_25,   25)
UPDATE_VALUE_TEST(update_v_50,   50)
UPDATE_VALUE_TEST(update_v_75,   75)
UPDATE_VALUE_TEST(update_v_100,  100)
UPDATE_VALUE_TEST(update_v_150,  150)
UPDATE_VALUE_TEST(update_v_200,  200)
UPDATE_VALUE_TEST(update_v_500,  500)
UPDATE_VALUE_TEST(update_v_1000, 1000)
UPDATE_VALUE_TEST(update_v_5000, 5000)
UPDATE_VALUE_TEST(update_v_10000,10000)
UPDATE_VALUE_TEST(update_v_50000,50000)

/* ============================================================ */
/* update_rtt: per-slot independence — updating one slot must    */
/* not affect any other slot.                                   */
/* ============================================================ */

#define UPDATE_SLOT_INDEP_TEST(name, slot) \
    START_TEST(name) { \
        struct rpc_rtt rt; \
        esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT); \
        esunrpc_rpc_update_rtt(&rt, slot, 500); \
        for (int i = 0; i < 5; i++) { \
            if (i == slot - 1) continue; \
            ck_assert_uint_eq(rt.srtt[i], 0); \
            ck_assert_uint_eq(rt.sdrtt[i], RPC_RTO_INIT); \
        } \
    } END_TEST

UPDATE_SLOT_INDEP_TEST(update_slot_indep_1, 1)
UPDATE_SLOT_INDEP_TEST(update_slot_indep_2, 2)
UPDATE_SLOT_INDEP_TEST(update_slot_indep_3, 3)
UPDATE_SLOT_INDEP_TEST(update_slot_indep_4, 4)
UPDATE_SLOT_INDEP_TEST(update_slot_indep_5, 5)

/* ============================================================ */
/* calc_rto for many timeo values when timer==0.                */
/* ============================================================ */

#define CALC_RTO_TIMEO_TEST(name, tv) \
    START_TEST(name) { \
        struct rpc_rtt rt; \
        esunrpc_rpc_init_rtt(&rt, (tv)); \
        ck_assert_uint_eq(esunrpc_rpc_calc_rto(&rt, 0), (tv)); \
    } END_TEST

CALC_RTO_TIMEO_TEST(calc_rto_t0_50,    50)
CALC_RTO_TIMEO_TEST(calc_rto_t0_100,   100)
CALC_RTO_TIMEO_TEST(calc_rto_t0_500,   500)
CALC_RTO_TIMEO_TEST(calc_rto_t0_1000,  1000)
CALC_RTO_TIMEO_TEST(calc_rto_t0_5000,  5000)
CALC_RTO_TIMEO_TEST(calc_rto_t0_30000, 30000)
CALC_RTO_TIMEO_TEST(calc_rto_t0_60000, 60000)

/* ============================================================ */
/* Convergence tests: feed N samples of m_jiffies, verify the   */
/* SRTT estimator converges towards the input. Van Jacobson's   */
/* SRTT_new = (7*SRTT_old + m) / 8. After enough samples the    */
/* estimator should track the true RTT.                          */
/* ============================================================ */

#define CONVERGE_TEST(name, m_val, samples, max_err) \
    START_TEST(name) { \
        struct rpc_rtt rt; \
        esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT); \
        for (int i = 0; i < (samples); i++) \
            esunrpc_rpc_update_rtt(&rt, 1, (m_val)); \
        /* SRTT is in 8x units (smoothed). */ \
        unsigned long actual = rt.srtt[0] >> 3; \
        unsigned long want   = (m_val); \
        unsigned long diff   = actual > want ? actual - want : want - actual; \
        ck_assert_uint_le(diff, (max_err)); \
    } END_TEST

CONVERGE_TEST(converge_to_10,    10,   100,  3)
CONVERGE_TEST(converge_to_20,    20,   100,  3)
CONVERGE_TEST(converge_to_50,    50,   100,  3)
CONVERGE_TEST(converge_to_100,   100,  100,  3)
CONVERGE_TEST(converge_to_500,   500,  100,  3)
CONVERGE_TEST(converge_to_1000,  1000, 100,  3)
CONVERGE_TEST(converge_to_50_long,    50,    500,  1)
CONVERGE_TEST(converge_to_100_long,   100,   500,  1)
CONVERGE_TEST(converge_to_500_long,   500,   500,  1)
CONVERGE_TEST(converge_to_1000_long, 1000,   500,  1)
CONVERGE_TEST(converge_to_2000_long, 2000,   500,  1)

/* ============================================================ */
/* calc_rto produces the same value as the RFC 6298 formula:    */
/* RTO = (SRTT >> 3) + SDRTT                                    */
/* ============================================================ */

START_TEST(calc_rto_formula_matches) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Feed a single sample to get non-zero SRTT/SDRTT. */
    esunrpc_rpc_update_rtt(&rt, 1, 100);
    unsigned long expected = ((rt.srtt[0] + 7) >> 3) + rt.sdrtt[0];
    if (expected > RPC_RTO_MAX) expected = RPC_RTO_MAX;
    ck_assert_uint_eq(esunrpc_rpc_calc_rto(&rt, 1), expected);
} END_TEST

/* ============================================================ */
/* Stability under noisy samples: alternating fast/slow RTTs.   */
/* The estimator should stay between the two extremes.          */
/* ============================================================ */

START_TEST(estimator_stays_between_extremes) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    for (int i = 0; i < 200; i++) {
        long m = (i & 1) ? 50 : 500;
        esunrpc_rpc_update_rtt(&rt, 1, m);
    }
    unsigned long srtt = rt.srtt[0] >> 3;
    ck_assert_uint_ge(srtt, 50);
    ck_assert_uint_le(srtt, 500);
} END_TEST

START_TEST(estimator_recovers_from_outlier) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* Steady at 100 for 100 samples. */
    for (int i = 0; i < 100; i++) esunrpc_rpc_update_rtt(&rt, 1, 100);
    /* One huge outlier. */
    esunrpc_rpc_update_rtt(&rt, 1, 10000);
    /* Recovery: 100 more steady samples. SRTT should pull back near 100. */
    for (int i = 0; i < 100; i++) esunrpc_rpc_update_rtt(&rt, 1, 100);
    unsigned long srtt = rt.srtt[0] >> 3;
    ck_assert_uint_ge(srtt, 95);
    ck_assert_uint_le(srtt, 110);
} END_TEST

/* ============================================================ */
/* SDRTT lower bound at RPC_RTO_MIN.                            */
/* ============================================================ */

START_TEST(sdrtt_clamped_to_minimum_after_steady_input) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    /* 1000 identical samples → mean-deviation should approach 0,
     * but the SUT clamps it to RPC_RTO_MIN. */
    for (int i = 0; i < 1000; i++) esunrpc_rpc_update_rtt(&rt, 1, 50);
    ck_assert_uint_ge(rt.sdrtt[0], RPC_RTO_MIN);
} END_TEST

/* ============================================================ */
/* Multi-slot independence: feeding different RTTs to different */
/* slots leaves them all consistent.                            */
/* ============================================================ */

START_TEST(multi_slot_carries_per_slot_history) {
    struct rpc_rtt rt;
    esunrpc_rpc_init_rtt(&rt, RPC_RTO_INIT);
    for (int i = 0; i < 100; i++) {
        esunrpc_rpc_update_rtt(&rt, 1, 50);
        esunrpc_rpc_update_rtt(&rt, 2, 200);
        esunrpc_rpc_update_rtt(&rt, 3, 1000);
        esunrpc_rpc_update_rtt(&rt, 4, 2000);
        esunrpc_rpc_update_rtt(&rt, 5, 5000);
    }
    unsigned long s1 = rt.srtt[0] >> 3;
    unsigned long s2 = rt.srtt[1] >> 3;
    unsigned long s3 = rt.srtt[2] >> 3;
    unsigned long s4 = rt.srtt[3] >> 3;
    unsigned long s5 = rt.srtt[4] >> 3;
    /* Each slot should be in its own neighbourhood. */
    ck_assert_uint_le(s1, 100);
    ck_assert_uint_ge(s2, 100); ck_assert_uint_le(s2, 300);
    ck_assert_uint_ge(s3, 800); ck_assert_uint_le(s3, 1200);
    ck_assert_uint_ge(s4, 1700); ck_assert_uint_le(s4, 2300);
    ck_assert_uint_ge(s5, 4500); ck_assert_uint_le(s5, 5500);
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

    /* Parameterised init_rtt at many timeo values. */
    TCase *tcit = tcase_create("init_timeo_param");
    tcase_add_test(tcit, init_timeo_50);
    tcase_add_test(tcit, init_timeo_100);
    tcase_add_test(tcit, init_timeo_150);
    tcase_add_test(tcit, init_timeo_200);
    tcase_add_test(tcit, init_timeo_300);
    tcase_add_test(tcit, init_timeo_500);
    tcase_add_test(tcit, init_timeo_750);
    tcase_add_test(tcit, init_timeo_1500);
    tcase_add_test(tcit, init_timeo_3000);
    tcase_add_test(tcit, init_timeo_6000);
    tcase_add_test(tcit, init_timeo_15000);
    tcase_add_test(tcit, init_timeo_30000);
    tcase_add_test(tcit, init_timeo_60000);
    suite_add_tcase(s, tcit);

    /* Parameterised single-update arithmetic at many m values. */
    TCase *tcuv = tcase_create("update_value_param");
    tcase_add_test(tcuv, update_v_1);
    tcase_add_test(tcuv, update_v_2);
    tcase_add_test(tcuv, update_v_5);
    tcase_add_test(tcuv, update_v_10);
    tcase_add_test(tcuv, update_v_25);
    tcase_add_test(tcuv, update_v_50);
    tcase_add_test(tcuv, update_v_75);
    tcase_add_test(tcuv, update_v_100);
    tcase_add_test(tcuv, update_v_150);
    tcase_add_test(tcuv, update_v_200);
    tcase_add_test(tcuv, update_v_500);
    tcase_add_test(tcuv, update_v_1000);
    tcase_add_test(tcuv, update_v_5000);
    tcase_add_test(tcuv, update_v_10000);
    tcase_add_test(tcuv, update_v_50000);
    suite_add_tcase(s, tcuv);

    /* Per-slot update independence. */
    TCase *tcsi = tcase_create("update_slot_indep");
    tcase_add_test(tcsi, update_slot_indep_1);
    tcase_add_test(tcsi, update_slot_indep_2);
    tcase_add_test(tcsi, update_slot_indep_3);
    tcase_add_test(tcsi, update_slot_indep_4);
    tcase_add_test(tcsi, update_slot_indep_5);
    suite_add_tcase(s, tcsi);

    /* calc_rto returns timeo on timer==0 — many timeo values. */
    TCase *tcrt = tcase_create("calc_rto_timeo_param");
    tcase_add_test(tcrt, calc_rto_t0_50);
    tcase_add_test(tcrt, calc_rto_t0_100);
    tcase_add_test(tcrt, calc_rto_t0_500);
    tcase_add_test(tcrt, calc_rto_t0_1000);
    tcase_add_test(tcrt, calc_rto_t0_5000);
    tcase_add_test(tcrt, calc_rto_t0_30000);
    tcase_add_test(tcrt, calc_rto_t0_60000);
    suite_add_tcase(s, tcrt);

    TCase *tcconv = tcase_create("convergence");
    tcase_add_test(tcconv, converge_to_10);
    tcase_add_test(tcconv, converge_to_20);
    tcase_add_test(tcconv, converge_to_50);
    tcase_add_test(tcconv, converge_to_100);
    tcase_add_test(tcconv, converge_to_500);
    tcase_add_test(tcconv, converge_to_1000);
    tcase_add_test(tcconv, converge_to_50_long);
    tcase_add_test(tcconv, converge_to_100_long);
    tcase_add_test(tcconv, converge_to_500_long);
    tcase_add_test(tcconv, converge_to_1000_long);
    tcase_add_test(tcconv, converge_to_2000_long);
    suite_add_tcase(s, tcconv);

    TCase *tcmisc = tcase_create("misc");
    tcase_add_test(tcmisc, calc_rto_formula_matches);
    tcase_add_test(tcmisc, estimator_stays_between_extremes);
    tcase_add_test(tcmisc, estimator_recovers_from_outlier);
    tcase_add_test(tcmisc, sdrtt_clamped_to_minimum_after_steady_input);
    tcase_add_test(tcmisc, multi_slot_carries_per_slot_history);
    suite_add_tcase(s, tcmisc);

    return s;
}

#define CHECK_RUNNER_SUITE  esunrpc_timer_suite
#include "check_runner.h"
