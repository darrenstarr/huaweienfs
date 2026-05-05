/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_enfs_path.c — Check tests for enfs_path.c (xprt-context
 * lifecycle).
 *
 * Two functions, both small: enfs_alloc_xprt_ctx allocates a fresh
 * enfs_xprt_context and binds it to the xprt's reserve slot;
 * enfs_free_xprt_ctx tears it down (incl. the iostats blob if any).
 *
 * Coverage:
 *   - alloc: NULL xprt rejected; valid xprt accepted; result is
 *     a fresh zero-initialised ctx; can be retrieved via the same
 *     xprt_get_reserve_context contract callers use
 *   - free: NULL ctx → safe no-op; ctx with no stats → ctx freed
 *     and slot reset to NULL; ctx with stats → both freed
 *   - paired: alloc → free idempotent; alloc → free → alloc cycle
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <linux/sunrpc/xprt.h>

#include "enfs.h"
#include "enfs_path.h"

extern void stub_reset_all(void);

int  enfs_alloc_xprt_ctx(struct rpc_xprt *xprt);
void enfs_free_xprt_ctx(struct rpc_xprt *xprt);

static struct rpc_xprt *make_xprt_bare(void)
{
    struct rpc_xprt *x = calloc(1, sizeof(*x));
    ck_assert_ptr_nonnull(x);
    kref_init(&x->kref);
    return x;
}

static void setup(void)    { stub_reset_all(); }
static void teardown(void) { /* fork-isolated */ }

/* ============================================================ */
/* enfs_alloc_xprt_ctx branches                                 */
/* ============================================================ */

START_TEST(alloc_NULL_xprt_returns_EINVAL) {
    int rc = enfs_alloc_xprt_ctx(NULL);
    ck_assert_int_eq(rc, -EINVAL);
} END_TEST

START_TEST(alloc_valid_xprt_returns_zero) {
    struct rpc_xprt *x = make_xprt_bare();
    ck_assert_int_eq(enfs_alloc_xprt_ctx(x), 0);
} END_TEST

START_TEST(alloc_installs_ctx_in_reserve_slot) {
    struct rpc_xprt *x = make_xprt_bare();
    enfs_alloc_xprt_ctx(x);
    void *ctx = xprt_get_reserve_context(x);
    ck_assert_ptr_nonnull(ctx);
} END_TEST

START_TEST(alloc_ctx_is_zero_initialised) {
    struct rpc_xprt *x = make_xprt_bare();
    enfs_alloc_xprt_ctx(x);
    struct enfs_xprt_context *ctx = xprt_get_reserve_context(x);
    /* main flag must default to false (zero), queuelen to 0,
     * path_state to 0 (PM_STATE_INIT). */
    ck_assert_int_eq(ctx->main, false);
    ck_assert_int_eq(atomic_long_read(&ctx->queuelen), 0);
    ck_assert_int_eq(atomic_read(&ctx->path_state), 0);
} END_TEST

START_TEST(alloc_returns_distinct_ctxs_for_distinct_xprts) {
    struct rpc_xprt *a = make_xprt_bare();
    struct rpc_xprt *b = make_xprt_bare();
    enfs_alloc_xprt_ctx(a);
    enfs_alloc_xprt_ctx(b);
    void *ca = xprt_get_reserve_context(a);
    void *cb = xprt_get_reserve_context(b);
    ck_assert_ptr_nonnull(ca);
    ck_assert_ptr_nonnull(cb);
    ck_assert_ptr_ne(ca, cb);
} END_TEST

/* ============================================================ */
/* enfs_free_xprt_ctx branches                                  */
/* ============================================================ */

START_TEST(free_no_ctx_is_safe_noop) {
    struct rpc_xprt *x = make_xprt_bare();
    /* Don't allocate; just free. Should not crash. */
    enfs_free_xprt_ctx(x);
    ck_assert_ptr_null(xprt_get_reserve_context(x));
} END_TEST

START_TEST(free_with_ctx_clears_reserve_slot) {
    struct rpc_xprt *x = make_xprt_bare();
    enfs_alloc_xprt_ctx(x);
    ck_assert_ptr_nonnull(xprt_get_reserve_context(x));
    enfs_free_xprt_ctx(x);
    ck_assert_ptr_null(xprt_get_reserve_context(x));
} END_TEST

/* free_with_stats_blob: SUT calls rpc_free_iostats(ctx->stats); the
 * stub in tests/stubs is a no-op, so we just verify it doesn't
 * crash and ctx slot is cleared. */
START_TEST(free_with_stats_does_not_crash) {
    struct rpc_xprt *x = make_xprt_bare();
    enfs_alloc_xprt_ctx(x);
    struct enfs_xprt_context *ctx = xprt_get_reserve_context(x);
    /* Pretend we have iostats. The stub's rpc_free_iostats just
     * frees whatever pointer we hand it. */
    ctx->stats = malloc(64);
    enfs_free_xprt_ctx(x);
    ck_assert_ptr_null(xprt_get_reserve_context(x));
} END_TEST

/* ============================================================ */
/* Paired alloc/free.                                           */
/* ============================================================ */

START_TEST(alloc_then_free_then_alloc_works) {
    struct rpc_xprt *x = make_xprt_bare();
    ck_assert_int_eq(enfs_alloc_xprt_ctx(x), 0);
    enfs_free_xprt_ctx(x);
    ck_assert_ptr_null(xprt_get_reserve_context(x));
    ck_assert_int_eq(enfs_alloc_xprt_ctx(x), 0);
    ck_assert_ptr_nonnull(xprt_get_reserve_context(x));
} END_TEST

START_TEST(many_alloc_free_cycles) {
    struct rpc_xprt *x = make_xprt_bare();
    for (int i = 0; i < 100; i++) {
        ck_assert_int_eq(enfs_alloc_xprt_ctx(x), 0);
        ck_assert_ptr_nonnull(xprt_get_reserve_context(x));
        enfs_free_xprt_ctx(x);
        ck_assert_ptr_null(xprt_get_reserve_context(x));
    }
} END_TEST

START_TEST(alloc_for_many_xprts_succeeds) {
    /* Stress: 64 xprts, all get valid ctxs. */
    struct rpc_xprt *xs[64];
    for (int i = 0; i < 64; i++) {
        xs[i] = make_xprt_bare();
        ck_assert_int_eq(enfs_alloc_xprt_ctx(xs[i]), 0);
        ck_assert_ptr_nonnull(xprt_get_reserve_context(xs[i]));
    }
    /* Free them all. */
    for (int i = 0; i < 64; i++) {
        enfs_free_xprt_ctx(xs[i]);
        ck_assert_ptr_null(xprt_get_reserve_context(xs[i]));
    }
} END_TEST

/* ============================================================ */
/* Suite plumbing.                                              */
/* ============================================================ */

static Suite *enfs_path_suite(void)
{
    Suite *s = suite_create("enfs_path");

    TCase *tca = tcase_create("alloc");
    tcase_add_checked_fixture(tca, setup, teardown);
    tcase_add_test(tca, alloc_NULL_xprt_returns_EINVAL);
    tcase_add_test(tca, alloc_valid_xprt_returns_zero);
    tcase_add_test(tca, alloc_installs_ctx_in_reserve_slot);
    tcase_add_test(tca, alloc_ctx_is_zero_initialised);
    tcase_add_test(tca, alloc_returns_distinct_ctxs_for_distinct_xprts);
    suite_add_tcase(s, tca);

    TCase *tcf = tcase_create("free");
    tcase_add_checked_fixture(tcf, setup, teardown);
    tcase_add_test(tcf, free_no_ctx_is_safe_noop);
    tcase_add_test(tcf, free_with_ctx_clears_reserve_slot);
    tcase_add_test(tcf, free_with_stats_does_not_crash);
    suite_add_tcase(s, tcf);

    TCase *tcp = tcase_create("paired");
    tcase_add_checked_fixture(tcp, setup, teardown);
    tcase_add_test(tcp, alloc_then_free_then_alloc_works);
    tcase_add_test(tcp, many_alloc_free_cycles);
    tcase_add_test(tcp, alloc_for_many_xprts_succeeds);
    suite_add_tcase(s, tcp);

    return s;
}

#define CHECK_RUNNER_SUITE  enfs_path_suite
#include "check_runner.h"
