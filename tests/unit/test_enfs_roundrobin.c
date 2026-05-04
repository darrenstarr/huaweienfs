// SPDX-License-Identifier: GPL-2.0
/*
 * test_enfs_roundrobin.c — Check tests for the round-robin and
 * singular xprt selection logic in
 * vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c.
 *
 * The source under test is compiled into this binary directly
 * (-Dstatic= exposes its `static` functions). Each test constructs
 * fake rpc_xprt_switch + rpc_xprt instances, attaches enfs_xprt_context
 * to each via xprt_set_reserve_context, sets path states via
 * stub_set_path_state, and asserts the right xprt gets selected.
 *
 * What's covered:
 *   - empty list → NULL
 *   - single active xprt → returns it
 *   - round-robin advances across multiple active xprts
 *   - inactive xprts (PM_STATE_FAULT, kref==0) are skipped
 *   - multipath-disabled config short-circuits to main xprt
 *   - singular policy returns next-active-from-cursor
 *   - revert path calls rpc_xprt_switch_set_singular
 *
 * What's NOT covered (and why):
 *   - Concurrency / RCU semantics — single-threaded harness
 *   - The native_link_io_status edge case combinations
 *     (left for follow-up; obvious extension once core works)
 */

#include <check.h>
#include <stdlib.h>
#include <stdio.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtmultipath.h>

/* ---------------------------------------------------------------- */
/* Forward decls of `static` functions under test, exposed by       */
/* compiling enfs_roundrobin.c with -Dstatic=. Re-declared here so  */
/* the test file knows their signatures without #including the .c.  */
/* ---------------------------------------------------------------- */
struct rpc_xprt *
enfs_lb_find_next_entry_roundrobin(struct rpc_xprt_switch *xps,
                                   const struct rpc_xprt *cur);

struct rpc_xprt *
enfs_lb_switch_get_next_xprt_roundrobin(struct rpc_xprt_switch *xps,
                                        const struct rpc_xprt *cur);

struct rpc_xprt *
enfs_lb_get_singular_xprt(struct rpc_xprt_switch *xps,
                          const struct rpc_xprt *cur);

/* `static` in production; sed-stripped in the SUT build. Exposed
 * here so the implicit-int rule doesn't sign-extend the returned
 * pointer to a truncated 32-bit value. */
struct rpc_xprt *
enfs_lb_iter_next_entry_roundrobin(struct rpc_xprt_iter *xpi);

struct rpc_xprt *
enfs_lb_iter_next_entry_sigular(struct rpc_xprt_iter *xpi);

struct rpc_xprt *
enfs_lb_switch_find_first_active_xprt(struct rpc_xprt_switch *xps);

struct rpc_xprt *
enfs_lb_switch_get_main_xprt(struct rpc_xprt_switch *xps);

int  enfs_lb_set_policy(struct rpc_clnt *clnt, void *data);
int  enfs_lb_revert_policy(struct rpc_clnt *clnt, void *data);
bool enfs_is_rr_route(struct rpc_clnt *clnt);
bool enfs_is_singularr_route(struct rpc_clnt *clnt);
const struct rpc_xprt_iter_ops *enfs_xprt_rr_ops(void);
const struct rpc_xprt_iter_ops *enfs_xprt_singular_ops(void);

/* ---------------------------------------------------------------- */
/* Stub control globals (exported by tests/stubs/enfs_deps_stubs.c) */
/* ---------------------------------------------------------------- */
extern int32_t stub_multipath_state;
extern int32_t stub_native_link_io_status;
extern int     stub_set_singular_calls;
extern int     stub_set_roundrobin_calls;

extern void stub_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state s);
extern void stub_reset_all(void);

/* ---------------------------------------------------------------- */
/* Test fixture helpers.                                            */
/* ---------------------------------------------------------------- */

/* Build an active xprt: kref=1, has a reserve context with the given
 * queuelen and main flag, path state = NORMAL by default. */
static struct rpc_xprt *
make_xprt(unsigned long queuelen, bool main, enum enfs_path_state state)
{
    struct rpc_xprt *xprt = calloc(1, sizeof(*xprt));
    ck_assert_ptr_nonnull(xprt);
    kref_init(&xprt->kref);                /* refcount = 1 */
    INIT_LIST_HEAD(&xprt->xprt_switch);

    struct enfs_xprt_context *ctx = calloc(1, sizeof(*ctx));
    ck_assert_ptr_nonnull(ctx);
    atomic_long_set(&ctx->queuelen, (long)queuelen);
    ctx->main = main;
    xprt_set_reserve_context(xprt, ctx);

    stub_set_path_state(xprt, state);
    return xprt;
}

static struct rpc_xprt_switch *make_xps(void)
{
    struct rpc_xprt_switch *xps = calloc(1, sizeof(*xps));
    ck_assert_ptr_nonnull(xps);
    spin_lock_init(&xps->xps_lock);
    INIT_LIST_HEAD(&xps->xps_xprt_list);
    xps->xps_nxprts = 0;
    return xps;
}

static void xps_add(struct rpc_xprt_switch *xps, struct rpc_xprt *xprt)
{
    list_add_tail(&xprt->xprt_switch, &xps->xps_xprt_list);
    xps->xps_nxprts++;
}

/* Per-test reset hook: Check fork-isolates each test, but resetting
 * is still cheaper than fork startup and clearer in intent. */
static void setup(void)   { stub_reset_all(); }
static void teardown(void) { /* fork-isolated; no need to free */ }

/* ---------------------------------------------------------------- */
/* Test cases.                                                      */
/* ---------------------------------------------------------------- */

START_TEST(empty_list_returns_null)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *got = enfs_lb_find_next_entry_roundrobin(xps, NULL);
    ck_assert_ptr_null(got);
}
END_TEST

START_TEST(single_active_xprt_returns_it)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(/*queuelen*/0, /*main*/false, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_xprt *got = enfs_lb_find_next_entry_roundrobin(xps, NULL);
    ck_assert_ptr_eq(got, a);
}
END_TEST

START_TEST(round_robin_advances_cursor)
{
    /* Three active xprts, all queuelen 0 → algorithm should pick
     * the one *after* cur. With cur=NULL the first iteration picks
     * the first xprt with min queuelen (which is the head). With
     * cur=that xprt, the next call picks the second. */
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *c = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b); xps_add(xps, c);

    /* Algorithm with min_queuelen==0 short-circuits to "return first
     * xprt found after cursor with queuelen==0". So the sequence
     * with cur null then cur=result is: pick after-cur with
     * queuelen 0. With cur=NULL, prev starts NULL, found becomes
     * true at the first iteration, so it returns `a`. */
    struct rpc_xprt *first = enfs_lb_find_next_entry_roundrobin(xps, NULL);
    ck_assert_ptr_eq(first, a);

    struct rpc_xprt *second = enfs_lb_find_next_entry_roundrobin(xps, a);
    ck_assert_ptr_eq(second, b);

    struct rpc_xprt *third = enfs_lb_find_next_entry_roundrobin(xps, b);
    ck_assert_ptr_eq(third, c);
}
END_TEST

START_TEST(inactive_xprt_is_never_returned)
{
    /* The algorithm uses `prev` tracking that includes inactive
     * xprts (prev is updated even when an inactive is skipped).
     * That means when an inactive sits between cur and the next
     * active, the `cur == prev` check never fires, and the function
     * falls through to "minimum-queuelen xprt seen so far" — which
     * with queuelen=0 everywhere is the first active, `a`.
     *
     * What's important to verify: an inactive xprt (b) is NEVER
     * returned by the selection function, no matter the cursor.
     */
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_FAULT);   /* inactive */
    struct rpc_xprt *c = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b); xps_add(xps, c);

    /* With cur set to the inactive xprt itself, found becomes true
     * after iter 2 (where pos=b matches cur). Iter 3 pos=c is then
     * active+found so c is returned. */
    struct rpc_xprt *got = enfs_lb_find_next_entry_roundrobin(xps, b);
    ck_assert_ptr_eq(got, c);

    /* With cur=NULL: queuelen-tied, picks first active. Importantly,
     * never returns b. */
    got = enfs_lb_find_next_entry_roundrobin(xps, NULL);
    ck_assert_ptr_ne(got, b);
    ck_assert_ptr_nonnull(got);
}
END_TEST

START_TEST(zero_kref_treated_as_inactive)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_NORMAL);
    /* Force-mark b as going-away. */
    atomic_set(&b->kref.refcount, 0);
    xps_add(xps, a); xps_add(xps, b);

    /* Asking for next-after-a should NOT return b (kref==0). With
     * only a being active, the round-robin algorithm picks a as
     * the min-queuelen xprt and returns it. */
    struct rpc_xprt *got = enfs_lb_find_next_entry_roundrobin(xps, a);
    ck_assert_ptr_eq(got, a);
}
END_TEST

START_TEST(multipath_disabled_returns_main)
{
    /* The wrapper enfs_lb_switch_get_next_xprt_roundrobin checks
     * the multipath-disabled config and short-circuits to the
     * head of xps_xprt_list. */
    stub_multipath_state = 1; /* ENFS_MULTIPATH_DISABLE */

    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true,  PM_STATE_NORMAL); /* main */
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b);

    struct rpc_xprt *got = enfs_lb_switch_get_next_xprt_roundrobin(xps, b);
    ck_assert_ptr_eq(got, a); /* main = first entry */
}
END_TEST

START_TEST(singular_returns_active_at_or_after_cursor)
{
    /* enfs_lb_switch_find_singular_entry sets `found=true` when
     * pos==cur and *immediately* returns if pos is active in the
     * same iteration. So singular returns:
     *   - the cursor itself, if it's active
     *   - otherwise, the first active xprt strictly after the cursor
     */
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_FAULT);
    struct rpc_xprt *c = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b); xps_add(xps, c);

    /* cur=a, active → returns a itself. */
    ck_assert_ptr_eq(enfs_lb_get_singular_xprt(xps, a), a);

    /* cur=b, inactive → walks past b to the next active = c. */
    ck_assert_ptr_eq(enfs_lb_get_singular_xprt(xps, b), c);

    /* cur=c, active → returns c. */
    ck_assert_ptr_eq(enfs_lb_get_singular_xprt(xps, c), c);
}
END_TEST

START_TEST(singular_with_null_cur_returns_first_active)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_FAULT); /* skip */
    struct rpc_xprt *b = make_xprt(0, true,  PM_STATE_NORMAL); /* main, active */
    struct rpc_xprt *c = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b); xps_add(xps, c);

    /* With cur=NULL OR xps_nxprts < 2, the singular path returns
     * the first active xprt found in the list. xps_nxprts is 3
     * here, but cur=NULL still exercises the "find_first_active"
     * branch. */
    struct rpc_xprt *got = enfs_lb_get_singular_xprt(xps, NULL);
    ck_assert_ptr_eq(got, b);
}
END_TEST

START_TEST(revert_policy_calls_set_singular)
{
    /* enfs_lb_revert_policy takes a clnt with cl_enfs=1 and calls
     * rpc_xprt_switch_set_singular on the underlying xps. The stub
     * records the call. */
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = xps;

    int ret = enfs_lb_revert_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(stub_set_singular_calls, 1);
}
END_TEST

START_TEST(revert_policy_skipped_when_not_enfs)
{
    /* cl_enfs == 0 → no call. */
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_clnt clnt = { .cl_enfs = 0, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = xps;

    int ret = enfs_lb_revert_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(stub_set_singular_calls, 0);
}
END_TEST

START_TEST(set_policy_v3_picks_roundrobin_ops)
{
    /* enfs_lb_set_policy on a v3 client with cl_enfs=1 should set
     * xps_iter_ops to the round-robin ops table. */
    struct rpc_xprt_switch *xps = make_xps();
    /* Need at least one xprt or the inner code returns early. */
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = xps;

    int ret = enfs_lb_set_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    ck_assert_ptr_eq(xps->xps_iter_ops, enfs_xprt_rr_ops());
    ck_assert(enfs_is_rr_route(&clnt));
    ck_assert(!enfs_is_singularr_route(&clnt));
}
END_TEST

START_TEST(set_policy_v4_picks_singular_ops)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 4 };
    clnt.cl_xpi.xpi_xpswitch = xps;

    int ret = enfs_lb_set_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    ck_assert_ptr_eq(xps->xps_iter_ops, enfs_xprt_singular_ops());
    ck_assert(enfs_is_singularr_route(&clnt));
    ck_assert(!enfs_is_rr_route(&clnt));
}
END_TEST

/* ================================================================ */
/* Branch-coverage tests: target conditions the basic suite missed. */
/* ================================================================ */

/* enfs_lb_find_next_entry_roundrobin: nativeLinkStatus==0 + main
 * xprt → main is skipped via line 70 `continue`. */
START_TEST(rr_skips_main_when_native_link_down)
{
    stub_native_link_io_status = 0;

    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *m = make_xprt(0, /*main*/true,  PM_STATE_NORMAL);
    struct rpc_xprt *a = make_xprt(0, /*main*/false, PM_STATE_NORMAL);
    xps_add(xps, m); xps_add(xps, a);

    /* With native link down, the main xprt (m) must be passed over.
     * cur=NULL → algorithm picks the first non-main active = a. */
    struct rpc_xprt *got = enfs_lb_find_next_entry_roundrobin(xps, NULL);
    ck_assert_ptr_eq(got, a);
    ck_assert_ptr_ne(got, m);
}
END_TEST

/* enfs_lb_find_next_entry_roundrobin: hits the optimal-with-nonzero-
 * queuelen path (lines 97-103). With non-zero queuelens the early
 * `if (min_xprt_queuelen == 0) return pos` doesn't fire, so the
 * algorithm assigns optimal_xprt and returns it after the loop. */
START_TEST(rr_picks_lowest_queuelen_optimal_path)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(/*qlen*/10, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(/*qlen*/ 5, false, PM_STATE_NORMAL);
    struct rpc_xprt *c = make_xprt(/*qlen*/20, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b); xps_add(xps, c);

    /* cur=a → found becomes true at iter for b. b has lower queuelen
     * (5) than the running min (10 from a). Optimal selection logic
     * (line 97-98) sets optimal_xprt=b, optimal_queuelen=5. c has
     * queuelen 20 > 5, so doesn't replace. Return optimal=b. */
    struct rpc_xprt *got = enfs_lb_find_next_entry_roundrobin(xps, a);
    ck_assert_ptr_eq(got, b);
}
END_TEST

/* enfs_lb_switch_find_first_active_xprt: all-inactive case returns
 * NULL (line 116). */
START_TEST(find_first_active_returns_null_when_all_inactive)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_FAULT);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_INIT);
    xps_add(xps, a); xps_add(xps, b);

    ck_assert_ptr_null(enfs_lb_switch_find_first_active_xprt(xps));
}
END_TEST

/* enfs_lb_switch_get_next_xprt_roundrobin (the wrapper):
 * with multipath ENABLED (state=0, default), reaches line 135
 * which calls find_next. Non-empty list returns from line 137. */
START_TEST(rr_wrapper_returns_find_next_result)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_xprt *got = enfs_lb_switch_get_next_xprt_roundrobin(xps, NULL);
    ck_assert_ptr_eq(got, a);
}
END_TEST

/* enfs_lb_switch_get_next_xprt_roundrobin: when find_next returns
 * NULL (empty list), falls through to enfs_lb_switch_get_main_xprt
 * (line 138 → which itself returns NULL on empty list). */
START_TEST(rr_wrapper_falls_back_to_main_on_empty)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *got = enfs_lb_switch_get_next_xprt_roundrobin(xps, NULL);
    ck_assert_ptr_null(got);
}
END_TEST

/* enfs_lb_get_singular_xprt: NULL xps → NULL (line 175). */
START_TEST(singular_null_xps_returns_null)
{
    ck_assert_ptr_null(enfs_lb_get_singular_xprt(NULL, NULL));
}
END_TEST

/* enfs_lb_get_singular_xprt: multipath disabled → main (line 178). */
START_TEST(singular_multipath_disabled_returns_main)
{
    stub_multipath_state = 1; /* DISABLE */
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b);

    struct rpc_xprt *got = enfs_lb_get_singular_xprt(xps, b);
    ck_assert_ptr_eq(got, a); /* head = main */
}
END_TEST

/* enfs_lb_get_singular_xprt: cur=NULL with no active xprts → falls
 * through "find_first_active returns NULL" → goto main_xprt
 * (lines 183, 195-196). With empty xps_xprt_list, main is NULL. */
START_TEST(singular_no_active_with_null_cur_returns_main)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_FAULT); /* inactive */
    xps_add(xps, a);

    /* xps_nxprts < 2 so the cur==NULL branch isn't strictly needed —
     * but xps_nxprts==1 also takes the find_first_active branch.
     * find_first_active returns NULL because a is inactive, so we
     * goto main_xprt and return main = a (head). */
    struct rpc_xprt *got = enfs_lb_get_singular_xprt(xps, NULL);
    ck_assert_ptr_eq(got, a); /* main = head, even if inactive */
}
END_TEST

/* enfs_lb_get_singular_xprt: find_singular_entry returns NULL
 * (cursor not found in list), find_first_active returns valid xprt
 * (line 189 path). */
START_TEST(singular_cursor_not_in_list_returns_first_active)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_NORMAL);
    /* Important: this xprt is NEVER added to xps. The cursor
     * pointing to it is therefore never found. */
    struct rpc_xprt *orphan = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b);

    struct rpc_xprt *got = enfs_lb_get_singular_xprt(xps, orphan);
    /* find_singular_entry walks list, never finds orphan → returns
     * NULL. Then find_first_active is called → returns a. */
    ck_assert_ptr_eq(got, a);
}
END_TEST

/* enfs_lb_get_singular_xprt: both find_singular AND find_first_active
 * return NULL → goto main_xprt → return head (line 191/196). */
START_TEST(singular_both_lookups_fail_returns_main)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_FAULT);   /* inactive */
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_FAULT);   /* inactive */
    struct rpc_xprt *orphan = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b);

    struct rpc_xprt *got = enfs_lb_get_singular_xprt(xps, orphan);
    /* find_singular: orphan never matches → returns NULL.
     * find_first_active: a, b both inactive → returns NULL.
     * goto main_xprt → returns head = a (regardless of activity). */
    ck_assert_ptr_eq(got, a);
}
END_TEST

/* enfs_lb_iter_next_entry_roundrobin: NULL xps → NULL (line 147). */
START_TEST(iter_next_rr_null_xps_returns_null)
{
    struct rpc_xprt_iter xpi = { .xpi_xpswitch = NULL, .xpi_cursor = NULL };
    ck_assert_ptr_null(enfs_lb_iter_next_entry_roundrobin(&xpi));
}
END_TEST

/* enfs_lb_iter_next_entry_roundrobin: real xps → updates cursor and
 * returns the selected xprt (lines 142-151 + enfs_lb_set_cursor_xprt). */
START_TEST(iter_next_rr_advances_cursor)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_xprt_iter xpi = { .xpi_xpswitch = xps, .xpi_cursor = NULL };

    struct rpc_xprt *got = enfs_lb_iter_next_entry_roundrobin(&xpi);
    ck_assert_ptr_eq(got, a);
    /* set_cursor_xprt should have updated xpi_cursor to the result. */
    ck_assert_ptr_eq(xpi.xpi_cursor, a);
}
END_TEST

/* enfs_lb_iter_next_entry_sigular: NULL xps → NULL (line 205). */
START_TEST(iter_next_singular_null_xps_returns_null)
{
    struct rpc_xprt_iter xpi = { .xpi_xpswitch = NULL, .xpi_cursor = NULL };
    ck_assert_ptr_null(enfs_lb_iter_next_entry_sigular(&xpi));
}
END_TEST

/* enfs_lb_iter_next_entry_sigular: real xps → updates cursor + returns. */
START_TEST(iter_next_singular_advances_cursor)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_xprt_iter xpi = { .xpi_xpswitch = xps, .xpi_cursor = NULL };
    struct rpc_xprt *got = enfs_lb_iter_next_entry_sigular(&xpi);
    ck_assert_ptr_eq(got, a);
    ck_assert_ptr_eq(xpi.xpi_cursor, a);
}
END_TEST

/* enfs_lb_iter_default_rewind: clears xpi_cursor (line 213). */
START_TEST(iter_rewind_clears_cursor)
{
    struct rpc_xprt dummy = { 0 };
    struct rpc_xprt_iter xpi = { .xpi_cursor = &dummy };
    /* Find rewind via the ops table — it's static otherwise. */
    enfs_xprt_rr_ops()->xpi_rewind(&xpi);
    ck_assert_ptr_null(xpi.xpi_cursor);

    /* Same op is shared by both ops tables. Verify: */
    xpi.xpi_cursor = &dummy;
    enfs_xprt_singular_ops()->xpi_rewind(&xpi);
    ck_assert_ptr_null(xpi.xpi_cursor);
}
END_TEST

/* enfs_lb_iter_current_entry: NULL xps → NULL (line 257). */
START_TEST(iter_current_null_xps_returns_null)
{
    struct rpc_xprt_iter xpi = { .xpi_xpswitch = NULL };
    /* iter_current_entry is static — reach it via the ops table. */
    ck_assert_ptr_null(enfs_xprt_rr_ops()->xpi_xprt(&xpi));
}
END_TEST

/* enfs_lb_iter_current_entry: cursor==NULL → returns main xprt
 * (line 260). Same with xps_nxprts < 2. */
START_TEST(iter_current_null_cursor_returns_main)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_xprt_iter xpi = { .xpi_xpswitch = xps, .xpi_cursor = NULL };
    struct rpc_xprt *got = enfs_xprt_rr_ops()->xpi_xprt(&xpi);
    ck_assert_ptr_eq(got, a); /* main = head */
}
END_TEST

/* enfs_lb_iter_current_entry: with cursor set and nxprts >= 2,
 * returns find_current(head, cursor) — exercises lines 261 and
 * enfs_lb_switch_find_current (lines 239-248). */
START_TEST(iter_current_with_cursor_returns_match)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); xps_add(xps, b);

    struct rpc_xprt_iter xpi = { .xpi_xpswitch = xps, .xpi_cursor = b };
    struct rpc_xprt *got = enfs_xprt_rr_ops()->xpi_xprt(&xpi);
    ck_assert_ptr_eq(got, b);

    /* Cursor not in list → find_current returns NULL. */
    struct rpc_xprt *orphan = make_xprt(0, false, PM_STATE_NORMAL);
    xpi.xpi_cursor = orphan;
    ck_assert_ptr_null(enfs_xprt_rr_ops()->xpi_xprt(&xpi));
}
END_TEST

/* enfs_lb_switch_set_roundrobin: NULL xps OR xps_nxprts==0 → return
 * early (line 225). The function is static, reachable via
 * enfs_lb_set_policy when cl_enfs=1 + cl_vers=3 + empty xps. */
START_TEST(set_policy_empty_xps_skips_iter_ops_set)
{
    struct rpc_xprt_switch *xps = make_xps(); /* xps_nxprts == 0 */
    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = xps;

    int ret = enfs_lb_set_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    /* xps_iter_ops should remain NULL — early return on
     * xps_nxprts == 0 prevents assignment. */
    ck_assert_ptr_null(xps->xps_iter_ops);
}
END_TEST

/* enfs_is_rr_route: NULL xps_iter_ops → false (line 303). */
START_TEST(is_rr_route_null_ops_returns_false)
{
    struct rpc_xprt_switch *xps = make_xps();
    /* xps_iter_ops left NULL */
    struct rpc_clnt clnt = { 0 };
    clnt.cl_xpi.xpi_xpswitch = xps;
    ck_assert(!enfs_is_rr_route(&clnt));
}
END_TEST

/* enfs_is_singularr_route: NULL xps_iter_ops → false (line 321). */
START_TEST(is_singular_route_null_ops_returns_false)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_clnt clnt = { 0 };
    clnt.cl_xpi.xpi_xpswitch = xps;
    ck_assert(!enfs_is_singularr_route(&clnt));
}
END_TEST

/* enfs_lb_init / enfs_lb_exit: trivial wrappers around
 * enfs_iter_rpc_clnt — observable via the stub's call counter. */
extern int stub_iter_rpc_clnt_calls;

int enfs_lb_init(void);
void enfs_lb_exit(void);

START_TEST(init_and_exit_call_iter_rpc_clnt)
{
    ck_assert_int_eq(stub_iter_rpc_clnt_calls, 0);
    int ret = enfs_lb_init();
    ck_assert_int_eq(ret, 0);
    ck_assert_int_eq(stub_iter_rpc_clnt_calls, 1);

    enfs_lb_exit();
    ck_assert_int_eq(stub_iter_rpc_clnt_calls, 2);
}
END_TEST

/* ================================================================ */
/* Branch-flip round: cover the "other direction" of conditionals   */
/* that the basic and branches suites only hit one side of.         */
/* ================================================================ */

/* Line 180: enfs_lb_get_singular_xprt — `cur == NULL || xps_nxprts < 2`.
 * Existing tests cover cur==NULL (left-true). Need the right-true:
 * cur != NULL AND xps_nxprts < 2 → take the find_first_active branch. */
START_TEST(singular_with_cur_and_nxprts_lt_2_returns_first_active)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, false, PM_STATE_NORMAL);
    xps_add(xps, a); /* xps_nxprts == 1 */

    struct rpc_xprt *got = enfs_lb_get_singular_xprt(xps, a);
    /* cur=a is non-NULL but nxprts==1 < 2 → falls into find_first_active
     * branch (not the find_singular branch) → returns a. */
    ck_assert_ptr_eq(got, a);
}
END_TEST

/* Line 224: enfs_lb_switch_set_roundrobin — `xps == NULL || nxprts == 0`.
 * Existing test set_policy_empty_xps_skips_iter_ops_set hits the
 * nxprts==0 path. Need the xps==NULL path. Reach via set_policy
 * with a clnt whose xpi_xpswitch is NULL. */
START_TEST(set_policy_null_xps_is_safe_noop)
{
    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = NULL;

    int ret = enfs_lb_set_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    /* Nothing crashed; nothing to assert beyond the call returning. */
}
END_TEST

/* Line 228: in enfs_lb_switch_set_roundrobin (v3 path) —
 * `if (READ_ONCE(xps_iter_ops) != &enfs_xprt_iter_roundrobin)`.
 * Need the case where iter_ops is ALREADY roundrobin → skip the
 * WRITE_ONCE. Pre-set the ops pointer, call again, verify no
 * adverse effect. */
START_TEST(set_policy_v3_idempotent_when_already_roundrobin)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    xps_add(xps, a);

    /* Run once to set the ops. */
    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = xps;
    enfs_lb_set_policy(&clnt, NULL);
    const struct rpc_xprt_iter_ops *first = xps->xps_iter_ops;

    /* Second call: iter_ops already == roundrobin → skip the write. */
    enfs_lb_set_policy(&clnt, NULL);
    ck_assert_ptr_eq(xps->xps_iter_ops, first);
}
END_TEST

/* Line 235: same idempotency for the v4/singular branch. */
START_TEST(set_policy_v4_idempotent_when_already_singular)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    xps_add(xps, a);

    struct rpc_clnt clnt = { .cl_enfs = 1, .cl_vers = 4 };
    clnt.cl_xpi.xpi_xpswitch = xps;
    enfs_lb_set_policy(&clnt, NULL);
    const struct rpc_xprt_iter_ops *first = xps->xps_iter_ops;

    enfs_lb_set_policy(&clnt, NULL);
    ck_assert_ptr_eq(xps->xps_iter_ops, first);
}
END_TEST

/* Line 259: enfs_lb_iter_current_entry —
 * `xpi_cursor == NULL || xps_nxprts < 2`. Existing tests cover
 * cursor==NULL. Need cursor != NULL AND xps_nxprts < 2 → returns
 * main (not find_current). */
START_TEST(iter_current_with_cursor_but_nxprts_lt_2_returns_main)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_xprt *a = make_xprt(0, true, PM_STATE_NORMAL);
    xps_add(xps, a); /* xps_nxprts == 1 */

    struct rpc_xprt_iter xpi = { .xpi_xpswitch = xps, .xpi_cursor = a };
    struct rpc_xprt *got = enfs_xprt_rr_ops()->xpi_xprt(&xpi);
    ck_assert_ptr_eq(got, a); /* main = head */
}
END_TEST

/* Line 266: enfs_lb_set_policy — `if (clnt->cl_enfs == 1)`. Existing
 * tests cover cl_enfs==1. Need cl_enfs != 1 → no-op. */
START_TEST(set_policy_skipped_when_not_enfs)
{
    struct rpc_xprt_switch *xps = make_xps();
    struct rpc_clnt clnt = { .cl_enfs = 0, .cl_vers = 3 };
    clnt.cl_xpi.xpi_xpswitch = xps;

    int ret = enfs_lb_set_policy(&clnt, NULL);
    ck_assert_int_eq(ret, 0);
    /* iter_ops should be untouched. */
    ck_assert_ptr_null(xps->xps_iter_ops);
}
END_TEST

/* Line 301: enfs_is_rr_route — `if (!xps || !xps->xps_iter_ops)`.
 * Existing test covers !iter_ops branch. Need !xps branch. */
START_TEST(is_rr_route_null_xps_returns_false)
{
    struct rpc_clnt clnt = { 0 };
    clnt.cl_xpi.xpi_xpswitch = NULL;
    ck_assert(!enfs_is_rr_route(&clnt));
}
END_TEST

/* Line 319: same for enfs_is_singularr_route. */
START_TEST(is_singular_route_null_xps_returns_false)
{
    struct rpc_clnt clnt = { 0 };
    clnt.cl_xpi.xpi_xpswitch = NULL;
    ck_assert(!enfs_is_singularr_route(&clnt));
}
END_TEST

/* Line 88: the `optimal_queuelen < min_xprt_queuelen` clause inside
 * `if (found && (optimal_xprt == NULL || ...))` is provably
 * unreachable by analysis:
 *
 *   - `min_xprt_queuelen` is monotonically non-increasing across the
 *     loop (it's a running minimum).
 *   - When `optimal_xprt` is first set, `optimal_queuelen` is assigned
 *     to `pos_xprt_queuelen`, which at that iteration equals the
 *     just-updated `min_xprt_queuelen` (the min update happened
 *     immediately before this assignment).
 *   - Therefore `optimal_queuelen >= min_xprt_queuelen` always holds
 *     after `optimal_xprt` is set, and `optimal_queuelen < min_xprt_queuelen`
 *     can never be true.
 *
 * Likely a copy-paste bug in the production code (the comparison was
 * probably meant to be `pos_xprt_queuelen < optimal_queuelen`). Not
 * fixing here — the framework's job is to test the code as-shipped,
 * and we're not authorized to modify vendor/openeuler/.
 *
 * Documented for the future: if the production code is ever fixed,
 * a test like `rr_picks_lowest_queuelen_optimal_path` with a 4-xprt
 * descending-queuelen sequence would naturally cover the corrected
 * comparison.
 */

/* ---------------------------------------------------------------- */
/* Suite plumbing.                                                  */
/* ---------------------------------------------------------------- */

static Suite *roundrobin_suite(void)
{
    Suite *s = suite_create("enfs_roundrobin");

    TCase *tc = tcase_create("selection");
    tcase_add_checked_fixture(tc, setup, teardown);
    tcase_add_test(tc, empty_list_returns_null);
    tcase_add_test(tc, single_active_xprt_returns_it);
    tcase_add_test(tc, round_robin_advances_cursor);
    tcase_add_test(tc, inactive_xprt_is_never_returned);
    tcase_add_test(tc, zero_kref_treated_as_inactive);
    tcase_add_test(tc, multipath_disabled_returns_main);
    tcase_add_test(tc, singular_returns_active_at_or_after_cursor);
    tcase_add_test(tc, singular_with_null_cur_returns_first_active);
    suite_add_tcase(s, tc);

    TCase *tc_policy = tcase_create("policy");
    tcase_add_checked_fixture(tc_policy, setup, teardown);
    tcase_add_test(tc_policy, revert_policy_calls_set_singular);
    tcase_add_test(tc_policy, revert_policy_skipped_when_not_enfs);
    tcase_add_test(tc_policy, set_policy_v3_picks_roundrobin_ops);
    tcase_add_test(tc_policy, set_policy_v4_picks_singular_ops);
    tcase_add_test(tc_policy, set_policy_empty_xps_skips_iter_ops_set);
    tcase_add_test(tc_policy, is_rr_route_null_ops_returns_false);
    tcase_add_test(tc_policy, is_singular_route_null_ops_returns_false);
    tcase_add_test(tc_policy, init_and_exit_call_iter_rpc_clnt);
    suite_add_tcase(s, tc_policy);

    /* Branch-coverage round: targets paths the basic suite missed. */
    TCase *tc_branches = tcase_create("branches");
    tcase_add_checked_fixture(tc_branches, setup, teardown);
    tcase_add_test(tc_branches, rr_skips_main_when_native_link_down);
    tcase_add_test(tc_branches, rr_picks_lowest_queuelen_optimal_path);
    tcase_add_test(tc_branches, find_first_active_returns_null_when_all_inactive);
    tcase_add_test(tc_branches, rr_wrapper_returns_find_next_result);
    tcase_add_test(tc_branches, rr_wrapper_falls_back_to_main_on_empty);
    tcase_add_test(tc_branches, singular_null_xps_returns_null);
    tcase_add_test(tc_branches, singular_multipath_disabled_returns_main);
    tcase_add_test(tc_branches, singular_no_active_with_null_cur_returns_main);
    tcase_add_test(tc_branches, singular_cursor_not_in_list_returns_first_active);
    tcase_add_test(tc_branches, singular_both_lookups_fail_returns_main);
    suite_add_tcase(s, tc_branches);

    /* Iterator ops table (xpi_*): exercise via the function pointers
     * that production callers invoke. */
    TCase *tc_iter = tcase_create("iter_ops");
    tcase_add_checked_fixture(tc_iter, setup, teardown);
    tcase_add_test(tc_iter, iter_next_rr_null_xps_returns_null);
    tcase_add_test(tc_iter, iter_next_rr_advances_cursor);
    tcase_add_test(tc_iter, iter_next_singular_null_xps_returns_null);
    tcase_add_test(tc_iter, iter_next_singular_advances_cursor);
    tcase_add_test(tc_iter, iter_rewind_clears_cursor);
    tcase_add_test(tc_iter, iter_current_null_xps_returns_null);
    tcase_add_test(tc_iter, iter_current_null_cursor_returns_main);
    tcase_add_test(tc_iter, iter_current_with_cursor_returns_match);
    suite_add_tcase(s, tc_iter);

    /* Branch-flip round: cover the "other side" of conditionals. */
    TCase *tc_flip = tcase_create("branch_flips");
    tcase_add_checked_fixture(tc_flip, setup, teardown);
    tcase_add_test(tc_flip, singular_with_cur_and_nxprts_lt_2_returns_first_active);
    tcase_add_test(tc_flip, set_policy_null_xps_is_safe_noop);
    tcase_add_test(tc_flip, set_policy_v3_idempotent_when_already_roundrobin);
    tcase_add_test(tc_flip, set_policy_v4_idempotent_when_already_singular);
    tcase_add_test(tc_flip, iter_current_with_cursor_but_nxprts_lt_2_returns_main);
    tcase_add_test(tc_flip, set_policy_skipped_when_not_enfs);
    tcase_add_test(tc_flip, is_rr_route_null_xps_returns_false);
    tcase_add_test(tc_flip, is_singular_route_null_xps_returns_false);
    suite_add_tcase(s, tc_flip);

    return s;
}

int main(void)
{
    Suite   *s  = roundrobin_suite();
    SRunner *sr = srunner_create(s);

    /* CK_VERBOSE prints per-test outcome. CI parses the trailing
     * pass/fail count from stdout. */
    srunner_run_all(sr, CK_VERBOSE);

    int failed = srunner_ntests_failed(sr);
    srunner_free(sr);
    return failed == 0 ? 0 : 1;
}
