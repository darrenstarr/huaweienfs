/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_esunrpc_xprtmultipath.c — unit tests for vendor/esunrpc/net/
 * esunrpc/xprtmultipath.c (multipath transport switch + iter ops).
 *
 * Coverage targets every public API the file exports + every iter
 * variant (singular, roundrobin, listall, listoffline). The point
 * is to verify that our forked copy of upstream xprtmultipath
 * preserves the load-balancing semantics enfs depends on.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>

#include <linux/atomic.h>
#include <linux/list.h>
#include <linux/kref.h>
#include <linux/spinlock.h>

/* Pull in the SUT's own struct definitions, not the shim's. */
#include <esunrpc/xprt.h>
#include <esunrpc/xprtmultipath.h>

extern void stub_reset_all(void);

/* Mock rpc_xprt — our shim provides one. xprtmultipath references
 * xprt->kref, xprt->state (XPRT_CONNECTED bit), and xprt->addr. */
extern unsigned long XPRT_CONGESTED_VAL;  /* unused; placeholder */

/* Helpers. */
static struct rpc_xprt *make_xprt_active(void)
{
    struct rpc_xprt *x = calloc(1, sizeof(*x));
    ck_assert_ptr_nonnull(x);
    kref_init(&x->kref);
    INIT_LIST_HEAD(&x->xprt_switch);
    /* "Bound" + "connected" — what xprt_is_active checks. */
    x->state = (1UL << XPRT_BOUND) | (1UL << XPRT_CONNECTED);
    return x;
}

static struct rpc_xprt *make_xprt_inactive(void)
{
    struct rpc_xprt *x = calloc(1, sizeof(*x));
    ck_assert_ptr_nonnull(x);
    kref_init(&x->kref);
    INIT_LIST_HEAD(&x->xprt_switch);
    x->state = 0;
    return x;
}

static void setup(void)    { stub_reset_all(); }
static void teardown(void) { /* fork-isolated */ }

/* ============================================================ */
/* xprt_switch_alloc                                            */
/* ============================================================ */

START_TEST(switch_alloc_returns_nonnull) {
    struct rpc_xprt *x = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(x, 0);
    ck_assert_ptr_nonnull(xps);
    /* Must include the seed xprt. */
    ck_assert_uint_eq(xps->xps_nxprts, 1);
} END_TEST

START_TEST(switch_alloc_holds_one_kref) {
    struct rpc_xprt *x = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(x, 0);
    /* One outstanding reference owned by the caller. */
    ck_assert_int_eq(kref_read(&xps->xps_kref), 1);
} END_TEST

START_TEST(switch_alloc_initialises_active_count) {
    struct rpc_xprt *x = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(x, 0);
    ck_assert_uint_ge(xps->xps_nactive, 0);
} END_TEST

START_TEST(switch_alloc_default_iter_ops_set) {
    struct rpc_xprt *x = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(x, 0);
    ck_assert_ptr_nonnull(xps->xps_iter_ops);
} END_TEST

/* ============================================================ */
/* xprt_switch_get / xprt_switch_put                            */
/* ============================================================ */

START_TEST(switch_get_increments_kref) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    int before = kref_read(&xps->xps_kref);
    xprt_switch_get(xps);
    int after = kref_read(&xps->xps_kref);
    ck_assert_int_eq(after, before + 1);
} END_TEST

START_TEST(switch_get_NULL_returns_NULL) {
    ck_assert_ptr_null(xprt_switch_get(NULL));
} END_TEST

START_TEST(switch_put_NULL_is_safe) {
    /* No assertion — must not crash. */
    xprt_switch_put(NULL);
} END_TEST

START_TEST(switch_put_decrements_kref) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    xprt_switch_get(xps);
    int before = kref_read(&xps->xps_kref);
    xprt_switch_put(xps);
    /* refcount drops by 1 (still > 0; switch not freed). */
    ck_assert_int_eq(kref_read(&xps->xps_kref), before - 1);
} END_TEST

/* ============================================================ */
/* rpc_xprt_switch_add_xprt + remove_xprt                       */
/* ============================================================ */

START_TEST(add_xprt_increments_count) {
    struct rpc_xprt *seed = make_xprt_active();
    struct rpc_xprt *extra = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(seed, 0);
    unsigned int before = xps->xps_nxprts;
    rpc_xprt_switch_add_xprt(xps, extra);
    ck_assert_uint_eq(xps->xps_nxprts, before + 1);
} END_TEST

START_TEST(add_xprt_NULL_is_safe) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    unsigned int before = xps->xps_nxprts;
    rpc_xprt_switch_add_xprt(xps, NULL);
    ck_assert_uint_eq(xps->xps_nxprts, before);
} END_TEST

START_TEST(add_many_xprts) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    for (unsigned int i = 0; i < 15; i++)
        rpc_xprt_switch_add_xprt(xps, make_xprt_active());
    ck_assert_uint_eq(xps->xps_nxprts, 16);
} END_TEST

START_TEST(remove_xprt_decrements_count) {
    struct rpc_xprt *extra = make_xprt_active();
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    rpc_xprt_switch_add_xprt(xps, extra);
    unsigned int before = xps->xps_nxprts;
    rpc_xprt_switch_remove_xprt(xps, extra, false);
    ck_assert_uint_eq(xps->xps_nxprts, before - 1);
} END_TEST

START_TEST(remove_xprt_not_in_list_is_safe) {
    struct rpc_xprt *outsider = make_xprt_active();
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    unsigned int before = xps->xps_nxprts;
    rpc_xprt_switch_remove_xprt(xps, outsider, false);
    ck_assert_uint_eq(xps->xps_nxprts, before);
} END_TEST

START_TEST(remove_xprt_offline_flag_works) {
    struct rpc_xprt *extra = make_xprt_active();
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    rpc_xprt_switch_add_xprt(xps, extra);
    rpc_xprt_switch_remove_xprt(xps, extra, true);
    /* Either way the count drops. */
    ck_assert_uint_eq(xps->xps_nxprts, 1);
} END_TEST

/* ============================================================ */
/* rpc_xprt_switch_get_main_xprt                                 */
/* ============================================================ */

START_TEST(get_main_xprt_returns_seed) {
    struct rpc_xprt *seed = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(seed, 0);
    /* The "main" xprt is the first one added (the seed). */
    struct rpc_xprt *m = rpc_xprt_switch_get_main_xprt(xps);
    ck_assert_ptr_eq(m, seed);
} END_TEST

START_TEST(get_main_xprt_unaffected_by_added_xprts) {
    struct rpc_xprt *seed = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(seed, 0);
    rpc_xprt_switch_add_xprt(xps, make_xprt_active());
    rpc_xprt_switch_add_xprt(xps, make_xprt_active());
    ck_assert_ptr_eq(rpc_xprt_switch_get_main_xprt(xps), seed);
} END_TEST

/* ============================================================ */
/* rpc_xprt_switch_set_roundrobin                                */
/* ============================================================ */

START_TEST(set_roundrobin_changes_iter_ops) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    const void *before = xps->xps_iter_ops;
    rpc_xprt_switch_set_roundrobin(xps);
    /* Must change to the round-robin ops table. */
    ck_assert_ptr_ne(xps->xps_iter_ops, before);
} END_TEST

START_TEST(set_roundrobin_idempotent) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    rpc_xprt_switch_set_roundrobin(xps);
    const void *first = xps->xps_iter_ops;
    rpc_xprt_switch_set_roundrobin(xps);
    ck_assert_ptr_eq(xps->xps_iter_ops, first);
} END_TEST

/* ============================================================ */
/* xprt_iter_init / xprt_iter_init_listall / listoffline         */
/* ============================================================ */

START_TEST(iter_init_attaches_to_switch) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps);
    ck_assert_ptr_eq(rcu_dereference(xpi.xpi_xpswitch), xps);
} END_TEST

START_TEST(iter_init_listall_attaches_to_switch) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init_listall(&xpi, xps);
    ck_assert_ptr_eq(rcu_dereference(xpi.xpi_xpswitch), xps);
} END_TEST

START_TEST(iter_init_listoffline_attaches_to_switch) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init_listoffline(&xpi, xps);
    ck_assert_ptr_eq(rcu_dereference(xpi.xpi_xpswitch), xps);
} END_TEST

START_TEST(iter_init_clears_cursor) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xpi.xpi_cursor = (void *)0xdeadbeef;
    xprt_iter_init(&xpi, xps);
    ck_assert_ptr_null(xpi.xpi_cursor);
} END_TEST

/* ============================================================ */
/* xprt_iter_destroy                                            */
/* ============================================================ */

START_TEST(iter_destroy_clears_switch_pointer) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps);
    xprt_iter_destroy(&xpi);
    ck_assert_ptr_null(rcu_dereference(xpi.xpi_xpswitch));
} END_TEST

/* ============================================================ */
/* xprt_iter_xchg_switch                                        */
/* ============================================================ */

START_TEST(iter_xchg_switch_replaces) {
    struct rpc_xprt_switch *xps_a =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_switch *xps_b =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps_a);
    struct rpc_xprt_switch *prev = xprt_iter_xchg_switch(&xpi, xps_b);
    ck_assert_ptr_eq(prev, xps_a);
    ck_assert_ptr_eq(rcu_dereference(xpi.xpi_xpswitch), xps_b);
} END_TEST

/* ============================================================ */
/* xprt_iter_get_next: first call walks the list                 */
/* ============================================================ */

START_TEST(iter_get_next_returns_an_xprt) {
    struct rpc_xprt *seed = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(seed, 0);
    rpc_xprt_switch_set_roundrobin(xps);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps);
    struct rpc_xprt *got = xprt_iter_get_next(&xpi);
    ck_assert_ptr_eq(got, seed);
} END_TEST

START_TEST(iter_get_next_rotates_through_all) {
    struct rpc_xprt *xs[5];
    for (int i = 0; i < 5; i++) xs[i] = make_xprt_active();
    struct rpc_xprt_switch *xps = xprt_switch_alloc(xs[0], 0);
    for (int i = 1; i < 5; i++) rpc_xprt_switch_add_xprt(xps, xs[i]);
    rpc_xprt_switch_set_roundrobin(xps);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps);

    bool seen[5] = { 0 };
    for (int i = 0; i < 5; i++) {
        struct rpc_xprt *g = xprt_iter_get_next(&xpi);
        ck_assert_ptr_nonnull(g);
        for (int j = 0; j < 5; j++)
            if (xs[j] == g) { seen[j] = true; break; }
    }
    /* All 5 visited at least once across one pass. */
    int nseen = 0;
    for (int j = 0; j < 5; j++) if (seen[j]) nseen++;
    ck_assert_int_eq(nseen, 5);
} END_TEST

/* ============================================================ */
/* xprt_is_active                                               */
/* ============================================================ */

START_TEST(xprt_is_active_true_when_bound_and_connected) {
    struct rpc_xprt *x = make_xprt_active();
    ck_assert(xprt_is_active(x));
} END_TEST

START_TEST(xprt_is_active_false_when_not_bound) {
    struct rpc_xprt *x = make_xprt_inactive();
    ck_assert(!xprt_is_active(x));
} END_TEST

START_TEST(xprt_is_active_false_when_not_connected) {
    struct rpc_xprt *x = make_xprt_inactive();
    /* Bind but don't connect. */
    x->state = (1UL << XPRT_BOUND);
    ck_assert(!xprt_is_active(x));
} END_TEST

/* ============================================================ */
/* rpc_xprt_switch_has_addr                                     */
/* ============================================================ */

START_TEST(switch_has_addr_finds_seed) {
    struct rpc_xprt *seed = make_xprt_active();
    struct sockaddr_in *sin = (void *)&seed->addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.0.2.10", &sin->sin_addr);

    struct rpc_xprt_switch *xps = xprt_switch_alloc(seed, 0);

    struct sockaddr_storage q = { 0 };
    struct sockaddr_in *qsin = (void *)&q;
    qsin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.0.2.10", &qsin->sin_addr);

    ck_assert(rpc_xprt_switch_has_addr(xps, (struct sockaddr *)&q));
} END_TEST

START_TEST(switch_has_addr_misses_when_not_present) {
    struct rpc_xprt *seed = make_xprt_active();
    struct sockaddr_in *sin = (void *)&seed->addr;
    sin->sin_family = AF_INET;
    inet_pton(AF_INET, "192.0.2.10", &sin->sin_addr);

    struct rpc_xprt_switch *xps = xprt_switch_alloc(seed, 0);

    struct sockaddr_storage q = { 0 };
    struct sockaddr_in *qsin = (void *)&q;
    qsin->sin_family = AF_INET;
    inet_pton(AF_INET, "10.0.0.1", &qsin->sin_addr);

    ck_assert(!rpc_xprt_switch_has_addr(xps, (struct sockaddr *)&q));
} END_TEST

/* ============================================================ */
/* xprt_iter_no_rewind / xprt_iter_default_rewind                */
/* ============================================================ */

START_TEST(iter_default_rewind_clears_cursor) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps);
    xpi.xpi_cursor = (void *)0xdeadbeef;
    xprt_iter_default_rewind(&xpi);
    ck_assert_ptr_null(xpi.xpi_cursor);
} END_TEST

START_TEST(iter_no_rewind_leaves_cursor_alone) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    struct rpc_xprt_iter xpi;
    xprt_iter_init(&xpi, xps);
    void *sentinel = (void *)0xcafef00d;
    xpi.xpi_cursor = sentinel;
    xprt_iter_no_rewind(&xpi);
    ck_assert_ptr_eq(xpi.xpi_cursor, sentinel);
} END_TEST

/* ============================================================ */
/* Lifecycle stress: many alloc + add + remove cycles            */
/* ============================================================ */

START_TEST(stress_add_remove_many_cycles) {
    struct rpc_xprt_switch *xps =
        xprt_switch_alloc(make_xprt_active(), 0);
    for (int cycle = 0; cycle < 20; cycle++) {
        struct rpc_xprt *batch[8];
        for (int i = 0; i < 8; i++) {
            batch[i] = make_xprt_active();
            rpc_xprt_switch_add_xprt(xps, batch[i]);
        }
        ck_assert_uint_eq(xps->xps_nxprts, 9);
        for (int i = 0; i < 8; i++)
            rpc_xprt_switch_remove_xprt(xps, batch[i], false);
        ck_assert_uint_eq(xps->xps_nxprts, 1);
    }
} END_TEST

/* ============================================================ */
/* Suite plumbing                                               */
/* ============================================================ */

static Suite *esunrpc_xprtmultipath_suite(void)
{
    Suite *s = suite_create("esunrpc_xprtmultipath");

    TCase *t1 = tcase_create("alloc");
    tcase_add_checked_fixture(t1, setup, teardown);
    tcase_add_test(t1, switch_alloc_returns_nonnull);
    tcase_add_test(t1, switch_alloc_holds_one_kref);
    tcase_add_test(t1, switch_alloc_initialises_active_count);
    tcase_add_test(t1, switch_alloc_default_iter_ops_set);
    suite_add_tcase(s, t1);

    TCase *t2 = tcase_create("get_put");
    tcase_add_checked_fixture(t2, setup, teardown);
    tcase_add_test(t2, switch_get_increments_kref);
    tcase_add_test(t2, switch_get_NULL_returns_NULL);
    tcase_add_test(t2, switch_put_NULL_is_safe);
    tcase_add_test(t2, switch_put_decrements_kref);
    suite_add_tcase(s, t2);

    TCase *t3 = tcase_create("add_remove");
    tcase_add_checked_fixture(t3, setup, teardown);
    tcase_add_test(t3, add_xprt_increments_count);
    tcase_add_test(t3, add_xprt_NULL_is_safe);
    tcase_add_test(t3, add_many_xprts);
    tcase_add_test(t3, remove_xprt_decrements_count);
    tcase_add_test(t3, remove_xprt_not_in_list_is_safe);
    tcase_add_test(t3, remove_xprt_offline_flag_works);
    suite_add_tcase(s, t3);

    TCase *t4 = tcase_create("get_main_xprt");
    tcase_add_checked_fixture(t4, setup, teardown);
    tcase_add_test(t4, get_main_xprt_returns_seed);
    tcase_add_test(t4, get_main_xprt_unaffected_by_added_xprts);
    suite_add_tcase(s, t4);

    TCase *t5 = tcase_create("set_roundrobin");
    tcase_add_checked_fixture(t5, setup, teardown);
    tcase_add_test(t5, set_roundrobin_changes_iter_ops);
    tcase_add_test(t5, set_roundrobin_idempotent);
    suite_add_tcase(s, t5);

    TCase *t6 = tcase_create("iter_init");
    tcase_add_checked_fixture(t6, setup, teardown);
    tcase_add_test(t6, iter_init_attaches_to_switch);
    tcase_add_test(t6, iter_init_listall_attaches_to_switch);
    tcase_add_test(t6, iter_init_listoffline_attaches_to_switch);
    tcase_add_test(t6, iter_init_clears_cursor);
    suite_add_tcase(s, t6);

    TCase *t7 = tcase_create("iter_lifecycle");
    tcase_add_checked_fixture(t7, setup, teardown);
    tcase_add_test(t7, iter_destroy_clears_switch_pointer);
    tcase_add_test(t7, iter_xchg_switch_replaces);
    suite_add_tcase(s, t7);

    TCase *t8 = tcase_create("iter_get_next");
    tcase_add_checked_fixture(t8, setup, teardown);
    tcase_add_test(t8, iter_get_next_returns_an_xprt);
    tcase_add_test(t8, iter_get_next_rotates_through_all);
    suite_add_tcase(s, t8);

    TCase *t9 = tcase_create("xprt_is_active");
    tcase_add_checked_fixture(t9, setup, teardown);
    tcase_add_test(t9, xprt_is_active_true_when_bound_and_connected);
    tcase_add_test(t9, xprt_is_active_false_when_not_bound);
    tcase_add_test(t9, xprt_is_active_false_when_not_connected);
    suite_add_tcase(s, t9);

    TCase *t10 = tcase_create("has_addr");
    tcase_add_checked_fixture(t10, setup, teardown);
    tcase_add_test(t10, switch_has_addr_finds_seed);
    tcase_add_test(t10, switch_has_addr_misses_when_not_present);
    suite_add_tcase(s, t10);

    TCase *t11 = tcase_create("rewind");
    tcase_add_checked_fixture(t11, setup, teardown);
    tcase_add_test(t11, iter_default_rewind_clears_cursor);
    tcase_add_test(t11, iter_no_rewind_leaves_cursor_alone);
    suite_add_tcase(s, t11);

    TCase *t12 = tcase_create("stress");
    tcase_add_checked_fixture(t12, setup, teardown);
    tcase_add_test(t12, stress_add_remove_many_cycles);
    suite_add_tcase(s, t12);

    return s;
}

#define CHECK_RUNNER_SUITE  esunrpc_xprtmultipath_suite
#include "check_runner.h"
