/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_pm_state.c — Check tests for pm_state.c
 *
 * Covers:
 *   - pm_get_path_state: NULL xprt, missing reserve ctx, valid xprt
 *     at every defined state
 *   - pm_set_path_state: idempotency, every state→state transition
 *   - enfs_is_path_connected (inline predicate): truth table for
 *     every enum value
 *
 * The implementation is small (~216 LOC) but every public function
 * has multiple branches we want to exercise. We do not test the
 * sockaddr_ip_to_str + diagnostic logging branch — those are for
 * dmesg output, not state semantics.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/clnt.h>

#include "enfs.h"
#include "pm_state.h"

/* Forward decls of the two functions defined in pm_state.c. */
enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt);
void pm_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state s);

extern void stub_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state s);
extern void stub_reset_all(void);

/* Helpers (mirror test_enfs_roundrobin.c's pattern). */
static struct rpc_xprt *
make_xprt_with_state(enum enfs_path_state state)
{
    struct rpc_xprt *xprt = calloc(1, sizeof(*xprt));
    ck_assert_ptr_nonnull(xprt);
    kref_init(&xprt->kref);
    INIT_LIST_HEAD(&xprt->xprt_switch);

    struct enfs_xprt_context *ctx = calloc(1, sizeof(*ctx));
    ck_assert_ptr_nonnull(ctx);
    atomic_set(&ctx->path_state, state);
    xprt_set_reserve_context(xprt, ctx);
    stub_set_path_state(xprt, state);
    return xprt;
}

static struct rpc_xprt *
make_xprt_without_ctx(void)
{
    struct rpc_xprt *xprt = calloc(1, sizeof(*xprt));
    ck_assert_ptr_nonnull(xprt);
    kref_init(&xprt->kref);
    INIT_LIST_HEAD(&xprt->xprt_switch);
    /* Deliberately do NOT install a reserve context — exercises
     * the ctx==NULL branch. */
    return xprt;
}

static void setup(void)    { stub_reset_all(); }
static void teardown(void) { /* fork-isolated */ }

/* ============================================================ */
/* pm_get_path_state branches                                   */
/* ============================================================ */

START_TEST(get_state_NULL_xprt_returns_UNDEFINED) {
    ck_assert_int_eq(pm_get_path_state(NULL), PM_STATE_UNDEFINED);
} END_TEST
START_TEST(get_state_no_ctx_returns_UNDEFINED) {
    struct rpc_xprt *x = make_xprt_without_ctx();
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_UNDEFINED);
} END_TEST
START_TEST(get_state_INIT_returns_INIT) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_INIT);
    /* Reads ctx->path_state via atomic_read — bypasses our stub. */
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_INIT);
} END_TEST
START_TEST(get_state_NORMAL_returns_NORMAL) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_NORMAL);
} END_TEST
START_TEST(get_state_UNSTABLE_returns_UNSTABLE) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_UNSTABLE);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_UNSTABLE);
} END_TEST
START_TEST(get_state_FAULT_returns_FAULT) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_FAULT);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_FAULT);
} END_TEST
START_TEST(get_state_UNDEFINED_returns_UNDEFINED) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_UNDEFINED);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_UNDEFINED);
} END_TEST

/* ============================================================ */
/* pm_set_path_state — idempotency: setting to the same state   */
/* must not change the value (and must hit the early-return on  */
/* `cur_state == state`).                                       */
/* ============================================================ */

#define IDEMPOTENT_TEST(state) \
    START_TEST(set_state_idempotent_##state) { \
        struct rpc_xprt *x = make_xprt_with_state(state); \
        pm_set_path_state(x, state); \
        ck_assert_int_eq(pm_get_path_state(x), state); \
    } END_TEST

IDEMPOTENT_TEST(PM_STATE_INIT)
IDEMPOTENT_TEST(PM_STATE_NORMAL)
IDEMPOTENT_TEST(PM_STATE_UNSTABLE)
IDEMPOTENT_TEST(PM_STATE_FAULT)
IDEMPOTENT_TEST(PM_STATE_UNDEFINED)

/* ============================================================ */
/* pm_set_path_state — every transition.                        */
/* 5 states × 5 states = 25 ordered pairs (5 of which are        */
/* idempotent, already covered above; we re-test all here for    */
/* uniformity — extra coverage is cheap).                       */
/* ============================================================ */

#define TRANSITION_TEST(from, to) \
    START_TEST(set_state_##from##_to_##to) { \
        struct rpc_xprt *x = make_xprt_with_state(from); \
        pm_set_path_state(x, to); \
        ck_assert_int_eq(pm_get_path_state(x), to); \
    } END_TEST

TRANSITION_TEST(PM_STATE_INIT,      PM_STATE_INIT)
TRANSITION_TEST(PM_STATE_INIT,      PM_STATE_NORMAL)
TRANSITION_TEST(PM_STATE_INIT,      PM_STATE_UNSTABLE)
TRANSITION_TEST(PM_STATE_INIT,      PM_STATE_FAULT)
TRANSITION_TEST(PM_STATE_INIT,      PM_STATE_UNDEFINED)

TRANSITION_TEST(PM_STATE_NORMAL,    PM_STATE_INIT)
TRANSITION_TEST(PM_STATE_NORMAL,    PM_STATE_NORMAL)
TRANSITION_TEST(PM_STATE_NORMAL,    PM_STATE_UNSTABLE)
TRANSITION_TEST(PM_STATE_NORMAL,    PM_STATE_FAULT)
TRANSITION_TEST(PM_STATE_NORMAL,    PM_STATE_UNDEFINED)

TRANSITION_TEST(PM_STATE_UNSTABLE,  PM_STATE_INIT)
TRANSITION_TEST(PM_STATE_UNSTABLE,  PM_STATE_NORMAL)
TRANSITION_TEST(PM_STATE_UNSTABLE,  PM_STATE_UNSTABLE)
TRANSITION_TEST(PM_STATE_UNSTABLE,  PM_STATE_FAULT)
TRANSITION_TEST(PM_STATE_UNSTABLE,  PM_STATE_UNDEFINED)

TRANSITION_TEST(PM_STATE_FAULT,     PM_STATE_INIT)
TRANSITION_TEST(PM_STATE_FAULT,     PM_STATE_NORMAL)
TRANSITION_TEST(PM_STATE_FAULT,     PM_STATE_UNSTABLE)
TRANSITION_TEST(PM_STATE_FAULT,     PM_STATE_FAULT)
TRANSITION_TEST(PM_STATE_FAULT,     PM_STATE_UNDEFINED)

TRANSITION_TEST(PM_STATE_UNDEFINED, PM_STATE_INIT)
TRANSITION_TEST(PM_STATE_UNDEFINED, PM_STATE_NORMAL)
TRANSITION_TEST(PM_STATE_UNDEFINED, PM_STATE_UNSTABLE)
TRANSITION_TEST(PM_STATE_UNDEFINED, PM_STATE_FAULT)
TRANSITION_TEST(PM_STATE_UNDEFINED, PM_STATE_UNDEFINED)

/* ============================================================ */
/* pm_set_path_state error branches.                            */
/* ============================================================ */

START_TEST(set_state_NULL_xprt_is_safe_noop) {
    /* Should not crash; nothing to assert about state. */
    pm_set_path_state(NULL, PM_STATE_NORMAL);
} END_TEST

START_TEST(set_state_no_ctx_is_safe_noop) {
    struct rpc_xprt *x = make_xprt_without_ctx();
    pm_set_path_state(x, PM_STATE_NORMAL);
    /* State remains UNDEFINED because there's no ctx to write to. */
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_UNDEFINED);
} END_TEST

/* ============================================================ */
/* enfs_is_path_connected truth table.                          */
/* From pm_state.h: returns true iff state is NORMAL or         */
/* UNSTABLE; everything else false.                             */
/* ============================================================ */

START_TEST(connected_NORMAL_true)    { ck_assert(enfs_is_path_connected(PM_STATE_NORMAL)); } END_TEST
START_TEST(connected_UNSTABLE_true)  { ck_assert(enfs_is_path_connected(PM_STATE_UNSTABLE)); } END_TEST
START_TEST(connected_INIT_false)     { ck_assert(!enfs_is_path_connected(PM_STATE_INIT)); } END_TEST
START_TEST(connected_FAULT_false)    { ck_assert(!enfs_is_path_connected(PM_STATE_FAULT)); } END_TEST
START_TEST(connected_UNDEFINED_false){ ck_assert(!enfs_is_path_connected(PM_STATE_UNDEFINED)); } END_TEST

/* ============================================================ */
/* Multi-step transition sequences — realistic state lifecycles. */
/* ============================================================ */

START_TEST(lifecycle_init_then_normal_then_fault_then_normal) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_INIT);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_INIT);
    pm_set_path_state(x, PM_STATE_NORMAL);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_NORMAL);
    pm_set_path_state(x, PM_STATE_FAULT);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_FAULT);
    pm_set_path_state(x, PM_STATE_NORMAL);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_NORMAL);
} END_TEST

START_TEST(lifecycle_normal_unstable_normal_flap) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    for (int i = 0; i < 100; i++) {
        pm_set_path_state(x, PM_STATE_UNSTABLE);
        ck_assert_int_eq(pm_get_path_state(x), PM_STATE_UNSTABLE);
        pm_set_path_state(x, PM_STATE_NORMAL);
        ck_assert_int_eq(pm_get_path_state(x), PM_STATE_NORMAL);
    }
} END_TEST

START_TEST(lifecycle_repeated_idempotent_writes) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    for (int i = 0; i < 1000; i++)
        pm_set_path_state(x, PM_STATE_NORMAL);
    ck_assert_int_eq(pm_get_path_state(x), PM_STATE_NORMAL);
} END_TEST

/* ============================================================ */
/* Multi-xprt independence — changes to one don't affect others. */
/* ============================================================ */

START_TEST(multi_xprt_state_independence) {
    struct rpc_xprt *a = make_xprt_with_state(PM_STATE_NORMAL);
    struct rpc_xprt *b = make_xprt_with_state(PM_STATE_FAULT);
    struct rpc_xprt *c = make_xprt_with_state(PM_STATE_INIT);
    ck_assert_int_eq(pm_get_path_state(a), PM_STATE_NORMAL);
    ck_assert_int_eq(pm_get_path_state(b), PM_STATE_FAULT);
    ck_assert_int_eq(pm_get_path_state(c), PM_STATE_INIT);
    pm_set_path_state(a, PM_STATE_FAULT);
    ck_assert_int_eq(pm_get_path_state(b), PM_STATE_FAULT);  /* unchanged */
    ck_assert_int_eq(pm_get_path_state(c), PM_STATE_INIT);   /* unchanged */
} END_TEST

START_TEST(multi_xprt_concurrent_transitions) {
    struct rpc_xprt *xs[10];
    for (int i = 0; i < 10; i++)
        xs[i] = make_xprt_with_state(PM_STATE_INIT);
    /* Each xprt independently transitions to NORMAL. */
    for (int i = 0; i < 10; i++)
        pm_set_path_state(xs[i], PM_STATE_NORMAL);
    for (int i = 0; i < 10; i++)
        ck_assert_int_eq(pm_get_path_state(xs[i]), PM_STATE_NORMAL);
} END_TEST

/* ============================================================ */
/* enfs_is_path_connected at every state.                        */
/* ============================================================ */

#define CONNECTED_TEST(name, state, expected) \
    START_TEST(name) { \
        ck_assert_int_eq(enfs_is_path_connected(state), expected); \
    } END_TEST

CONNECTED_TEST(connected_init_false2,      PM_STATE_INIT,      false)
CONNECTED_TEST(connected_normal_true2,     PM_STATE_NORMAL,    true)
CONNECTED_TEST(connected_unstable_true2,   PM_STATE_UNSTABLE,  true)
CONNECTED_TEST(connected_fault_false2,     PM_STATE_FAULT,     false)
CONNECTED_TEST(connected_undefined_false2, PM_STATE_UNDEFINED, false)

/* ============================================================ */
/* Long-running transition sequences (stress).                   */
/* ============================================================ */

START_TEST(stress_random_transitions_100) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    enum enfs_path_state seq[] = {
        PM_STATE_FAULT, PM_STATE_NORMAL, PM_STATE_UNSTABLE,
        PM_STATE_NORMAL, PM_STATE_INIT, PM_STATE_NORMAL,
        PM_STATE_FAULT, PM_STATE_INIT, PM_STATE_UNSTABLE,
        PM_STATE_NORMAL,
    };
    for (int rep = 0; rep < 10; rep++)
        for (size_t i = 0; i < sizeof(seq)/sizeof(seq[0]); i++) {
            pm_set_path_state(x, seq[i]);
            ck_assert_int_eq(pm_get_path_state(x), seq[i]);
        }
} END_TEST

START_TEST(stress_alternating_normal_fault_500) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    for (int i = 0; i < 500; i++) {
        pm_set_path_state(x, (i & 1) ? PM_STATE_FAULT : PM_STATE_NORMAL);
        ck_assert_int_eq(pm_get_path_state(x),
                         (i & 1) ? PM_STATE_FAULT : PM_STATE_NORMAL);
    }
} END_TEST

START_TEST(stress_walk_all_5_states_in_cycle) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_INIT);
    enum enfs_path_state cycle[] = {
        PM_STATE_NORMAL, PM_STATE_UNSTABLE, PM_STATE_FAULT,
        PM_STATE_INIT, PM_STATE_UNDEFINED,
    };
    for (int rep = 0; rep < 50; rep++)
        for (size_t i = 0; i < 5; i++) {
            pm_set_path_state(x, cycle[i]);
            ck_assert_int_eq(pm_get_path_state(x), cycle[i]);
        }
} END_TEST

/* ============================================================ */
/* Many xprts × many transitions.                                */
/* ============================================================ */

START_TEST(stress_many_xprts_independent_lifecycles) {
    const int N = 50;
    struct rpc_xprt *xs[N];
    for (int i = 0; i < N; i++)
        xs[i] = make_xprt_with_state(PM_STATE_INIT);
    /* Push each through INIT → NORMAL → FAULT → NORMAL. */
    for (int i = 0; i < N; i++) pm_set_path_state(xs[i], PM_STATE_NORMAL);
    for (int i = 0; i < N; i++) ck_assert_int_eq(pm_get_path_state(xs[i]), PM_STATE_NORMAL);
    for (int i = 0; i < N; i++) pm_set_path_state(xs[i], PM_STATE_FAULT);
    for (int i = 0; i < N; i++) ck_assert_int_eq(pm_get_path_state(xs[i]), PM_STATE_FAULT);
    for (int i = 0; i < N; i++) pm_set_path_state(xs[i], PM_STATE_NORMAL);
    for (int i = 0; i < N; i++) ck_assert_int_eq(pm_get_path_state(xs[i]), PM_STATE_NORMAL);
} END_TEST

/* ============================================================ */
/* get_state on NULL input never crashes (already covered, but   */
/* exercise it from inside a long run.)                          */
/* ============================================================ */

START_TEST(stress_NULL_calls_repeated) {
    for (int i = 0; i < 1000; i++) {
        ck_assert_int_eq(pm_get_path_state(NULL), PM_STATE_UNDEFINED);
        pm_set_path_state(NULL, PM_STATE_NORMAL);
    }
} END_TEST

/* ============================================================ */
/* pm_get_path_state_desc — renders the path state as a string. */
/* Tests the full switch + safety branches.                     */
/* ============================================================ */

void pm_get_path_state_desc(struct rpc_xprt *xprt, char *buf, int len);
void pm_get_xprt_state_desc(struct rpc_xprt *xprt, char *buf, int len);

START_TEST(path_desc_INIT_renders_Init) {
    char buf[16] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_INIT);
    pm_get_path_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "Init");
} END_TEST

START_TEST(path_desc_NORMAL_renders_Normal) {
    char buf[16] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_path_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "Normal");
} END_TEST

START_TEST(path_desc_UNSTABLE_renders_Unstable) {
    char buf[16] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_UNSTABLE);
    pm_get_path_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "Unstable");
} END_TEST

START_TEST(path_desc_FAULT_renders_Fault) {
    char buf[16] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_FAULT);
    pm_get_path_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "Fault");
} END_TEST

START_TEST(path_desc_UNDEFINED_renders_Unknown) {
    char buf[16] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_UNDEFINED);
    pm_get_path_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "Unknown");
} END_TEST

START_TEST(path_desc_NULL_xprt_no_op_buf_unchanged) {
    char buf[16] = "PRESERVED";
    pm_get_path_state_desc(NULL, buf, sizeof(buf));
    ck_assert_str_eq(buf, "PRESERVED");
} END_TEST

START_TEST(path_desc_NULL_buf_no_op) {
    /* Just ensure no crash. */
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_path_state_desc(x, NULL, 16);
} END_TEST

START_TEST(path_desc_zero_len_no_op) {
    char buf[16] = "PRESERVED";
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_path_state_desc(x, buf, 0);
    ck_assert_str_eq(buf, "PRESERVED");
} END_TEST

START_TEST(path_desc_negative_len_no_op) {
    char buf[16] = "PRESERVED";
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_path_state_desc(x, buf, -1);
    ck_assert_str_eq(buf, "PRESERVED");
} END_TEST

/* Render to a buffer that's TOO small — snprintf truncates. */
START_TEST(path_desc_short_buffer_truncates) {
    char buf[3] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_path_state_desc(x, buf, sizeof(buf));
    /* "Normal" → buf gets at most 2 chars + NUL: "No". */
    ck_assert_int_eq(strlen(buf), 2);
    ck_assert_int_eq(buf[0], 'N');
} END_TEST

START_TEST(path_desc_no_ctx_renders_Unknown) {
    /* xprt with no enfs ctx → pm_get_path_state returns UNDEFINED
     * which falls into the default branch → "Unknown". */
    char buf[16] = {0};
    struct rpc_xprt *x = make_xprt_without_ctx();
    pm_get_path_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "Unknown");
} END_TEST

/* ============================================================ */
/* pm_get_xprt_state_desc — renders xprt state bits as          */
/* PIPE-separated names (LOCKED|CONNECTED|...).                 */
/* ============================================================ */

START_TEST(xprt_desc_no_bits_yields_empty) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = 0;
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "");
} END_TEST

START_TEST(xprt_desc_LOCKED_only) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_LOCKED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "LOCKED");
} END_TEST

START_TEST(xprt_desc_CONNECTED_only) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_CONNECTED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "CONNECTED");
} END_TEST

START_TEST(xprt_desc_BOUND_only) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_BOUND);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "BOUND");
} END_TEST

/* The mask iteration order in the SUT is: LOCKED, CONNECTED,
 * CONNECTING, CLOSE_WAIT, BOUND, BINDING, CLOSING, CONGESTED.
 * So when several bits are set the rendered string follows that order. */
START_TEST(xprt_desc_LOCKED_and_CONNECTED) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_LOCKED) | (1UL << XPRT_CONNECTED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "LOCKED|CONNECTED");
} END_TEST

START_TEST(xprt_desc_BOUND_and_CONNECTED_orders_CONNECTED_first) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_BOUND) | (1UL << XPRT_CONNECTED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    /* CONNECTED comes before BOUND in the SUT's iteration order. */
    ck_assert_str_eq(buf, "CONNECTED|BOUND");
} END_TEST

START_TEST(xprt_desc_all_bits_set_full_pipe_string) {
    char buf[128] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_LOCKED) | (1UL << XPRT_CONNECTED)
             | (1UL << XPRT_CONNECTING) | (1UL << XPRT_CLOSE_WAIT)
             | (1UL << XPRT_BOUND) | (1UL << XPRT_BINDING)
             | (1UL << XPRT_CLOSING) | (1UL << XPRT_CONGESTED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf,
        "LOCKED|CONNECTED|CONNECTING|CLOSE_WAIT|"
        "BOUND|BINDING|CLOSING|CONGESTED");
} END_TEST

START_TEST(xprt_desc_NULL_xprt_buf_unchanged) {
    char buf[64] = "PRESERVED";
    pm_get_xprt_state_desc(NULL, buf, sizeof(buf));
    ck_assert_str_eq(buf, "PRESERVED");
} END_TEST

START_TEST(xprt_desc_NULL_buf_no_op) {
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_xprt_state_desc(x, NULL, 64);
} END_TEST

START_TEST(xprt_desc_zero_len_no_op) {
    char buf[64] = "PRESERVED";
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    pm_get_xprt_state_desc(x, buf, 0);
    ck_assert_str_eq(buf, "PRESERVED");
} END_TEST

/* Buffer-truncation: short buffer cuts off mid-render. */
START_TEST(xprt_desc_short_buffer_first_token_only) {
    char buf[10] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_LOCKED) | (1UL << XPRT_CONNECTED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    /* "LOCKED" (6 chars) fits, but "|CONNECTED" (10) needs another 9
     * after the NUL — short buffer should truncate within. */
    ck_assert_int_lt(strlen(buf), sizeof(buf));
    ck_assert_int_eq(strncmp(buf, "LOCKED", 6), 0);
} END_TEST

/* Parametric: every single-bit setting renders exactly the
 * matching name. */
#define XPRT_DESC_SINGLE_BIT(name, bit, expected) \
    START_TEST(name) { \
        char buf[64] = {0}; \
        struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL); \
        x->state = (1UL << (bit)); \
        pm_get_xprt_state_desc(x, buf, sizeof(buf)); \
        ck_assert_str_eq(buf, expected); \
    } END_TEST

XPRT_DESC_SINGLE_BIT(xprt_desc_p_LOCKED,     XPRT_LOCKED,     "LOCKED")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_CONNECTED,  XPRT_CONNECTED,  "CONNECTED")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_CONNECTING, XPRT_CONNECTING, "CONNECTING")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_CLOSE_WAIT, XPRT_CLOSE_WAIT, "CLOSE_WAIT")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_BOUND,      XPRT_BOUND,      "BOUND")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_BINDING,    XPRT_BINDING,    "BINDING")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_CLOSING,    XPRT_CLOSING,    "CLOSING")
XPRT_DESC_SINGLE_BIT(xprt_desc_p_CONGESTED,  XPRT_CONGESTED,  "CONGESTED")

/* Bit combinations matrix. The SUT iterates in fixed order so the
 * output is deterministic. */
START_TEST(xprt_desc_three_bits_locked_connected_bound) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_LOCKED) | (1UL << XPRT_CONNECTED) | (1UL << XPRT_BOUND);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "LOCKED|CONNECTED|BOUND");
} END_TEST

START_TEST(xprt_desc_connecting_binding) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_CONNECTING) | (1UL << XPRT_BINDING);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "CONNECTING|BINDING");
} END_TEST

START_TEST(xprt_desc_closing_close_wait_congested) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_CLOSING) | (1UL << XPRT_CLOSE_WAIT) | (1UL << XPRT_CONGESTED);
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "CLOSE_WAIT|CLOSING|CONGESTED");
} END_TEST

/* Bits OUTSIDE the recognised set are silently dropped. */
START_TEST(xprt_desc_unknown_bit_ignored) {
    char buf[64] = {0};
    struct rpc_xprt *x = make_xprt_with_state(PM_STATE_NORMAL);
    x->state = (1UL << XPRT_LOCKED) | (1UL << 30);  /* bit 30 not recognised */
    pm_get_xprt_state_desc(x, buf, sizeof(buf));
    ck_assert_str_eq(buf, "LOCKED");
} END_TEST

/* ============================================================ */
/* Suite plumbing.                                              */
/* ============================================================ */

static Suite *pm_state_suite(void)
{
    Suite *s = suite_create("pm_state");

    TCase *tcg = tcase_create("get_state");
    tcase_add_checked_fixture(tcg, setup, teardown);
    tcase_add_test(tcg, get_state_NULL_xprt_returns_UNDEFINED);
    tcase_add_test(tcg, get_state_no_ctx_returns_UNDEFINED);
    tcase_add_test(tcg, get_state_INIT_returns_INIT);
    tcase_add_test(tcg, get_state_NORMAL_returns_NORMAL);
    tcase_add_test(tcg, get_state_UNSTABLE_returns_UNSTABLE);
    tcase_add_test(tcg, get_state_FAULT_returns_FAULT);
    tcase_add_test(tcg, get_state_UNDEFINED_returns_UNDEFINED);
    suite_add_tcase(s, tcg);

    TCase *tci = tcase_create("set_state_idempotent");
    tcase_add_checked_fixture(tci, setup, teardown);
    tcase_add_test(tci, set_state_idempotent_PM_STATE_INIT);
    tcase_add_test(tci, set_state_idempotent_PM_STATE_NORMAL);
    tcase_add_test(tci, set_state_idempotent_PM_STATE_UNSTABLE);
    tcase_add_test(tci, set_state_idempotent_PM_STATE_FAULT);
    tcase_add_test(tci, set_state_idempotent_PM_STATE_UNDEFINED);
    suite_add_tcase(s, tci);

    TCase *tct = tcase_create("set_state_transitions");
    tcase_add_checked_fixture(tct, setup, teardown);
    tcase_add_test(tct, set_state_PM_STATE_INIT_to_PM_STATE_INIT);
    tcase_add_test(tct, set_state_PM_STATE_INIT_to_PM_STATE_NORMAL);
    tcase_add_test(tct, set_state_PM_STATE_INIT_to_PM_STATE_UNSTABLE);
    tcase_add_test(tct, set_state_PM_STATE_INIT_to_PM_STATE_FAULT);
    tcase_add_test(tct, set_state_PM_STATE_INIT_to_PM_STATE_UNDEFINED);
    tcase_add_test(tct, set_state_PM_STATE_NORMAL_to_PM_STATE_INIT);
    tcase_add_test(tct, set_state_PM_STATE_NORMAL_to_PM_STATE_NORMAL);
    tcase_add_test(tct, set_state_PM_STATE_NORMAL_to_PM_STATE_UNSTABLE);
    tcase_add_test(tct, set_state_PM_STATE_NORMAL_to_PM_STATE_FAULT);
    tcase_add_test(tct, set_state_PM_STATE_NORMAL_to_PM_STATE_UNDEFINED);
    tcase_add_test(tct, set_state_PM_STATE_UNSTABLE_to_PM_STATE_INIT);
    tcase_add_test(tct, set_state_PM_STATE_UNSTABLE_to_PM_STATE_NORMAL);
    tcase_add_test(tct, set_state_PM_STATE_UNSTABLE_to_PM_STATE_UNSTABLE);
    tcase_add_test(tct, set_state_PM_STATE_UNSTABLE_to_PM_STATE_FAULT);
    tcase_add_test(tct, set_state_PM_STATE_UNSTABLE_to_PM_STATE_UNDEFINED);
    tcase_add_test(tct, set_state_PM_STATE_FAULT_to_PM_STATE_INIT);
    tcase_add_test(tct, set_state_PM_STATE_FAULT_to_PM_STATE_NORMAL);
    tcase_add_test(tct, set_state_PM_STATE_FAULT_to_PM_STATE_UNSTABLE);
    tcase_add_test(tct, set_state_PM_STATE_FAULT_to_PM_STATE_FAULT);
    tcase_add_test(tct, set_state_PM_STATE_FAULT_to_PM_STATE_UNDEFINED);
    tcase_add_test(tct, set_state_PM_STATE_UNDEFINED_to_PM_STATE_INIT);
    tcase_add_test(tct, set_state_PM_STATE_UNDEFINED_to_PM_STATE_NORMAL);
    tcase_add_test(tct, set_state_PM_STATE_UNDEFINED_to_PM_STATE_UNSTABLE);
    tcase_add_test(tct, set_state_PM_STATE_UNDEFINED_to_PM_STATE_FAULT);
    tcase_add_test(tct, set_state_PM_STATE_UNDEFINED_to_PM_STATE_UNDEFINED);
    suite_add_tcase(s, tct);

    TCase *tce = tcase_create("set_state_errors");
    tcase_add_checked_fixture(tce, setup, teardown);
    tcase_add_test(tce, set_state_NULL_xprt_is_safe_noop);
    tcase_add_test(tce, set_state_no_ctx_is_safe_noop);
    suite_add_tcase(s, tce);

    TCase *tcc = tcase_create("connected_predicate");
    tcase_add_checked_fixture(tcc, setup, teardown);
    tcase_add_test(tcc, connected_NORMAL_true);
    tcase_add_test(tcc, connected_UNSTABLE_true);
    tcase_add_test(tcc, connected_INIT_false);
    tcase_add_test(tcc, connected_FAULT_false);
    tcase_add_test(tcc, connected_UNDEFINED_false);
    suite_add_tcase(s, tcc);

    TCase *tcl = tcase_create("lifecycles");
    tcase_add_checked_fixture(tcl, setup, teardown);
    tcase_add_test(tcl, lifecycle_init_then_normal_then_fault_then_normal);
    tcase_add_test(tcl, lifecycle_normal_unstable_normal_flap);
    tcase_add_test(tcl, lifecycle_repeated_idempotent_writes);
    suite_add_tcase(s, tcl);

    /* Multi-xprt independence. */
    TCase *tcm = tcase_create("multi_xprt");
    tcase_add_checked_fixture(tcm, setup, teardown);
    tcase_add_test(tcm, multi_xprt_state_independence);
    tcase_add_test(tcm, multi_xprt_concurrent_transitions);
    suite_add_tcase(s, tcm);

    /* Connected predicate at every state — second pass for redundancy. */
    TCase *tccp = tcase_create("connected_predicate_redux");
    tcase_add_checked_fixture(tccp, setup, teardown);
    tcase_add_test(tccp, connected_init_false2);
    tcase_add_test(tccp, connected_normal_true2);
    tcase_add_test(tccp, connected_unstable_true2);
    tcase_add_test(tccp, connected_fault_false2);
    tcase_add_test(tccp, connected_undefined_false2);
    suite_add_tcase(s, tccp);

    TCase *tcd = tcase_create("path_state_desc");
    tcase_add_checked_fixture(tcd, setup, teardown);
    tcase_add_test(tcd, path_desc_INIT_renders_Init);
    tcase_add_test(tcd, path_desc_NORMAL_renders_Normal);
    tcase_add_test(tcd, path_desc_UNSTABLE_renders_Unstable);
    tcase_add_test(tcd, path_desc_FAULT_renders_Fault);
    tcase_add_test(tcd, path_desc_UNDEFINED_renders_Unknown);
    tcase_add_test(tcd, path_desc_NULL_xprt_no_op_buf_unchanged);
    tcase_add_test(tcd, path_desc_NULL_buf_no_op);
    tcase_add_test(tcd, path_desc_zero_len_no_op);
    tcase_add_test(tcd, path_desc_negative_len_no_op);
    tcase_add_test(tcd, path_desc_short_buffer_truncates);
    tcase_add_test(tcd, path_desc_no_ctx_renders_Unknown);
    suite_add_tcase(s, tcd);

    TCase *tcx = tcase_create("xprt_state_desc");
    tcase_add_checked_fixture(tcx, setup, teardown);
    tcase_add_test(tcx, xprt_desc_no_bits_yields_empty);
    tcase_add_test(tcx, xprt_desc_LOCKED_only);
    tcase_add_test(tcx, xprt_desc_CONNECTED_only);
    tcase_add_test(tcx, xprt_desc_BOUND_only);
    tcase_add_test(tcx, xprt_desc_LOCKED_and_CONNECTED);
    tcase_add_test(tcx, xprt_desc_BOUND_and_CONNECTED_orders_CONNECTED_first);
    tcase_add_test(tcx, xprt_desc_all_bits_set_full_pipe_string);
    tcase_add_test(tcx, xprt_desc_NULL_xprt_buf_unchanged);
    tcase_add_test(tcx, xprt_desc_NULL_buf_no_op);
    tcase_add_test(tcx, xprt_desc_zero_len_no_op);
    tcase_add_test(tcx, xprt_desc_short_buffer_first_token_only);
    tcase_add_test(tcx, xprt_desc_p_LOCKED);
    tcase_add_test(tcx, xprt_desc_p_CONNECTED);
    tcase_add_test(tcx, xprt_desc_p_CONNECTING);
    tcase_add_test(tcx, xprt_desc_p_CLOSE_WAIT);
    tcase_add_test(tcx, xprt_desc_p_BOUND);
    tcase_add_test(tcx, xprt_desc_p_BINDING);
    tcase_add_test(tcx, xprt_desc_p_CLOSING);
    tcase_add_test(tcx, xprt_desc_p_CONGESTED);
    tcase_add_test(tcx, xprt_desc_three_bits_locked_connected_bound);
    tcase_add_test(tcx, xprt_desc_connecting_binding);
    tcase_add_test(tcx, xprt_desc_closing_close_wait_congested);
    tcase_add_test(tcx, xprt_desc_unknown_bit_ignored);
    suite_add_tcase(s, tcx);

    /* Long-running stress. */
    TCase *tcs = tcase_create("stress");
    tcase_add_checked_fixture(tcs, setup, teardown);
    tcase_add_test(tcs, stress_random_transitions_100);
    tcase_add_test(tcs, stress_alternating_normal_fault_500);
    tcase_add_test(tcs, stress_walk_all_5_states_in_cycle);
    tcase_add_test(tcs, stress_many_xprts_independent_lifecycles);
    tcase_add_test(tcs, stress_NULL_calls_repeated);
    suite_add_tcase(s, tcs);

    return s;
}

#define CHECK_RUNNER_SUITE  pm_state_suite
#include "check_runner.h"
