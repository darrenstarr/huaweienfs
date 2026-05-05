/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_failover_time.c — unit tests for vendor/openeuler/fs/nfs/
 * enfs/failover_time.c.
 *
 * The SUT has three functions and one static helper:
 *
 *   failover_get_mulitipath_timeout(clnt) — clamp config_tmo to
 *     min(config_tmo, clnt->cl_timeout->to_initval); zero config
 *     means "use clnt->cl_timeout->to_initval verbatim".
 *
 *   failover_adjust_task_timeout(task, condition) — clamp
 *     task->tk_timeout to the multipath timeout when the client is
 *     enfs-managed and multipath is enabled. Short-circuits on
 *     disabled/null/non-enfs.
 *
 *   get_normal_io_req_timeout(req) — compute the major-timeo for
 *     a normal request: rq_timeout × 2^retries (exponential backoff)
 *     OR rq_timeout + retries × increment (linear). Saturate at
 *     to_maxval; treat 0 as overflow.
 *
 *   failover_init_task_req(task, req) — set rq_timeout +
 *     rq_majortimeo for either a probe or a normal request, taking
 *     elapsed-time into account.
 *
 * Tests construct minimal rpc_clnt / rpc_task / rpc_rqst on the
 * stack and observe the post-call field values.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <linux/types.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>

/* SUT signatures (also exposed from failover_time.h). */
void failover_adjust_task_timeout(struct rpc_task *task, void *condition);
void failover_init_task_req(struct rpc_task *task, struct rpc_rqst *req);

/* Static helpers exposed via -Dstatic= in the SUT compile. */
unsigned long failover_get_mulitipath_timeout(struct rpc_clnt *clnt);
unsigned long get_normal_io_req_timeout(struct rpc_rqst *req);

/* Stub control surface. */
extern int  stub_multipath_state;
extern long stub_multipath_timeout_secs;
extern long stub_path_detect_timeout_secs;
extern bool stub_is_test_xprt_task;
extern long long stub_ktime_now;
extern unsigned int stub_ktime_ms_delta;
extern unsigned long jiffies;
extern void stub_failover_time_reset(void);

/* === Helpers: build a fully-wired clnt/task/req stack ============== */

static struct rpc_timeout *make_timeout(unsigned long initval,
                                         unsigned long maxval,
                                         unsigned char exp,
                                         unsigned long incr,
                                         unsigned int retries)
{
    struct rpc_timeout *to = calloc(1, sizeof(*to));
    to->to_initval     = initval;
    to->to_maxval      = maxval;
    to->to_exponential = exp;
    to->to_increment   = incr;
    to->to_retries     = retries;
    return to;
}

static struct rpc_clnt *make_clnt(int enfs_flag,
                                   struct rpc_timeout *to)
{
    struct rpc_clnt *c = calloc(1, sizeof(*c));
    c->cl_enfs    = enfs_flag;
    c->cl_parent  = NULL;
    c->cl_timeout = to;
    return c;
}

static struct rpc_task *make_task(struct rpc_clnt *clnt, unsigned long tmo)
{
    struct rpc_task *t = calloc(1, sizeof(*t));
    t->tk_client  = clnt;
    t->tk_timeout = tmo;
    t->tk_start   = 0;
    return t;
}

static struct rpc_rqst *make_req(struct rpc_task *t, unsigned long rq_tmo)
{
    struct rpc_rqst *r = calloc(1, sizeof(*r));
    r->rq_task    = t;
    r->rq_timeout = rq_tmo;
    return r;
}

static void common_setup(void) { stub_failover_time_reset(); }

/* ============================================================ */
/* failover_get_mulitipath_timeout                              */
/* ============================================================ */

START_TEST(get_tmo_zero_config_returns_clnt_initval) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 0;
    ck_assert_uint_eq(failover_get_mulitipath_timeout(c), 60 * HZ);
} END_TEST

START_TEST(get_tmo_smaller_config_wins) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;  /* 30s < 60s clnt initval */
    ck_assert_uint_eq(failover_get_mulitipath_timeout(c), 30 * HZ);
} END_TEST

START_TEST(get_tmo_larger_config_clamped_to_clnt) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 120; /* 120s > 60s, clamp */
    ck_assert_uint_eq(failover_get_mulitipath_timeout(c), 60 * HZ);
} END_TEST

START_TEST(get_tmo_equal_returns_either) {
    common_setup();
    struct rpc_timeout *to = make_timeout(45 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 45;
    ck_assert_uint_eq(failover_get_mulitipath_timeout(c), 45 * HZ);
} END_TEST

/* Parametric matrix: every (config, clnt_initval) pair confirms
 * the min-of-the-two semantics. */
#define GET_TMO_TEST(name, cfg_secs, initval_jif, expected) \
    START_TEST(name) { \
        common_setup(); \
        struct rpc_timeout *to = make_timeout(initval_jif, 1000 * HZ, 1, 0, 5); \
        struct rpc_clnt *c = make_clnt(1, to); \
        stub_multipath_timeout_secs = (cfg_secs); \
        ck_assert_uint_eq(failover_get_mulitipath_timeout(c), (expected)); \
    } END_TEST

GET_TMO_TEST(get_tmo_p_0_60,    0,  60 * HZ, 60 * HZ)
GET_TMO_TEST(get_tmo_p_5_60,    5,  60 * HZ,  5 * HZ)
GET_TMO_TEST(get_tmo_p_10_60,  10,  60 * HZ, 10 * HZ)
GET_TMO_TEST(get_tmo_p_30_60,  30,  60 * HZ, 30 * HZ)
GET_TMO_TEST(get_tmo_p_59_60,  59,  60 * HZ, 59 * HZ)
GET_TMO_TEST(get_tmo_p_60_60,  60,  60 * HZ, 60 * HZ)
GET_TMO_TEST(get_tmo_p_61_60,  61,  60 * HZ, 60 * HZ)
GET_TMO_TEST(get_tmo_p_120_60,120,  60 * HZ, 60 * HZ)
GET_TMO_TEST(get_tmo_p_300_60,300,  60 * HZ, 60 * HZ)
GET_TMO_TEST(get_tmo_p_1_30,    1,  30 * HZ,  1 * HZ)
GET_TMO_TEST(get_tmo_p_15_30,  15,  30 * HZ, 15 * HZ)
GET_TMO_TEST(get_tmo_p_29_30,  29,  30 * HZ, 29 * HZ)
GET_TMO_TEST(get_tmo_p_30_30,  30,  30 * HZ, 30 * HZ)
GET_TMO_TEST(get_tmo_p_31_30,  31,  30 * HZ, 30 * HZ)
GET_TMO_TEST(get_tmo_p_5_120,   5, 120 * HZ,  5 * HZ)
GET_TMO_TEST(get_tmo_p_119_120,119,120 * HZ,119 * HZ)
GET_TMO_TEST(get_tmo_p_120_120,120,120 * HZ,120 * HZ)
GET_TMO_TEST(get_tmo_p_121_120,121,120 * HZ,120 * HZ)
GET_TMO_TEST(get_tmo_p_240_120,240,120 * HZ,120 * HZ)
GET_TMO_TEST(get_tmo_p_3600_120,3600,120 * HZ,120 * HZ)
GET_TMO_TEST(get_tmo_p_2_3,     2,   3 * HZ,  2 * HZ)
GET_TMO_TEST(get_tmo_p_3_3,     3,   3 * HZ,  3 * HZ)
GET_TMO_TEST(get_tmo_p_4_3,     4,   3 * HZ,  3 * HZ)
GET_TMO_TEST(get_tmo_p_0_300,   0, 300 * HZ,300 * HZ)
GET_TMO_TEST(get_tmo_p_0_1,     0,   1 * HZ,  1 * HZ)

/* ============================================================ */
/* failover_adjust_task_timeout                                 */
/* ============================================================ */

START_TEST(adjust_disabled_multipath_is_no_op) {
    common_setup();
    stub_multipath_state = 0;  /* != ENFS_MULTIPATH_ENABLE */
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 999);
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 999);  /* unchanged */
} END_TEST

START_TEST(adjust_null_clnt_is_no_op) {
    common_setup();
    struct rpc_task *t = make_task(NULL, 777);
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 777);
} END_TEST

START_TEST(adjust_non_enfs_clnt_is_no_op) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(0, to);  /* cl_enfs = 0 */
    struct rpc_task *t = make_task(c, 555);
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 555);
} END_TEST

START_TEST(adjust_enfs_clamps_when_task_tmo_larger) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;
    struct rpc_task *t = make_task(c, 100 * HZ); /* > 30s clamp */
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 30 * HZ);
} END_TEST

START_TEST(adjust_enfs_keeps_smaller_task_tmo) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;
    struct rpc_task *t = make_task(c, 5 * HZ);  /* < 30s, keep */
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 5 * HZ);
} END_TEST

START_TEST(adjust_zero_task_tmo_takes_multipath) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;
    struct rpc_task *t = make_task(c, 0);
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 30 * HZ);
} END_TEST

#define ADJUST_TEST(name, cfg_secs, init_jif, task_tmo, expected) \
    START_TEST(name) { \
        common_setup(); \
        struct rpc_timeout *to = make_timeout(init_jif, 1000 * HZ, 1, 0, 5); \
        struct rpc_clnt *c = make_clnt(1, to); \
        stub_multipath_timeout_secs = (cfg_secs); \
        struct rpc_task *t = make_task(c, (task_tmo)); \
        failover_adjust_task_timeout(t, NULL); \
        ck_assert_uint_eq(t->tk_timeout, (expected)); \
    } END_TEST

ADJUST_TEST(adj_p_a, 30, 60 * HZ,   1 * HZ,  1 * HZ)
ADJUST_TEST(adj_p_b, 30, 60 * HZ,  10 * HZ, 10 * HZ)
ADJUST_TEST(adj_p_c, 30, 60 * HZ,  29 * HZ, 29 * HZ)
ADJUST_TEST(adj_p_d, 30, 60 * HZ,  30 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_e, 30, 60 * HZ,  31 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_f, 30, 60 * HZ,  60 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_g, 30, 60 * HZ, 100 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_h, 60, 30 * HZ,   1 * HZ,  1 * HZ)
ADJUST_TEST(adj_p_i, 60, 30 * HZ,  29 * HZ, 29 * HZ)
ADJUST_TEST(adj_p_j, 60, 30 * HZ,  30 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_k, 60, 30 * HZ,  31 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_l, 60, 30 * HZ,  60 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_m,  0, 30 * HZ,  10 * HZ, 10 * HZ)
ADJUST_TEST(adj_p_n,  0, 30 * HZ,  60 * HZ, 30 * HZ)
ADJUST_TEST(adj_p_o,  0, 30 * HZ,       0,  30 * HZ)
ADJUST_TEST(adj_p_p, 10, 60 * HZ,  20 * HZ, 10 * HZ)
ADJUST_TEST(adj_p_q, 10, 60 * HZ,   3 * HZ,  3 * HZ)

/* ============================================================ */
/* get_normal_io_req_timeout                                    */
/* ============================================================ */

START_TEST(io_tmo_exp_zero_retries_returns_input) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 5 * HZ);
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 5 * HZ);
} END_TEST

START_TEST(io_tmo_exp_one_retry_doubles) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 1);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 5 * HZ);
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 10 * HZ);
} END_TEST

START_TEST(io_tmo_exp_three_retries_x8) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 3);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 5 * HZ);
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 40 * HZ);
} END_TEST

START_TEST(io_tmo_exp_saturates_at_maxval) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 100 * HZ, 1, 0, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 50 * HZ);  /* 50<<5 = 1600 > 100 */
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 100 * HZ);
} END_TEST

START_TEST(io_tmo_linear_zero_retries) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 0, 5 * HZ, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 10 * HZ);
    /* 10 + 0*5 = 10 */
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 10 * HZ);
} END_TEST

START_TEST(io_tmo_linear_three_retries) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 0, 5 * HZ, 3);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 10 * HZ);
    /* 10 + 3*5 = 25 */
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 25 * HZ);
} END_TEST

START_TEST(io_tmo_linear_saturates_at_maxval) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 50 * HZ, 0, 100 * HZ, 5);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 10 * HZ);  /* 10 + 5*100 = 510 > 50 */
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 50 * HZ);
} END_TEST

START_TEST(io_tmo_overflow_clamped_to_maxval) {
    /* Cause the multiplication to overflow to 0 — picked up by the
     * `== 0` clamp in the SUT. */
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 99 * HZ, 1, 0, 63);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 1);  /* 1 << 63 = 0 (signed) → clamp */
    /* Either the >maxval branch or the ==0 branch runs; both clamp. */
    ck_assert_uint_eq(get_normal_io_req_timeout(r), 99 * HZ);
} END_TEST

#define IO_EXP_TEST(name, retries, in_jif, max_jif, expected) \
    START_TEST(name) { \
        common_setup(); \
        struct rpc_timeout *to = make_timeout(60 * HZ, max_jif, 1, 0, retries); \
        struct rpc_clnt *c = make_clnt(1, to); \
        struct rpc_task *t = make_task(c, 0); \
        struct rpc_rqst *r = make_req(t, in_jif); \
        ck_assert_uint_eq(get_normal_io_req_timeout(r), expected); \
    } END_TEST

IO_EXP_TEST(io_exp_2x,  1, 10 * HZ, 1000 * HZ,  20 * HZ)
IO_EXP_TEST(io_exp_4x,  2, 10 * HZ, 1000 * HZ,  40 * HZ)
IO_EXP_TEST(io_exp_8x,  3, 10 * HZ, 1000 * HZ,  80 * HZ)
IO_EXP_TEST(io_exp_16x, 4, 10 * HZ, 1000 * HZ, 160 * HZ)
IO_EXP_TEST(io_exp_32x, 5, 10 * HZ, 1000 * HZ, 320 * HZ)
IO_EXP_TEST(io_exp_clamp_64,    6,  10 * HZ,  300 * HZ, 300 * HZ)
IO_EXP_TEST(io_exp_clamp_128,   7,  10 * HZ,  300 * HZ, 300 * HZ)
IO_EXP_TEST(io_exp_zero_in_zero, 0, 0, 1000 * HZ, 1000 * HZ) /* 0 == 0 → clamp */
IO_EXP_TEST(io_exp_in_8,  3,  8 * HZ, 1000 * HZ,  64 * HZ)

#define IO_LIN_TEST(name, retries, incr_jif, in_jif, max_jif, expected) \
    START_TEST(name) { \
        common_setup(); \
        struct rpc_timeout *to = make_timeout(60 * HZ, max_jif, 0, incr_jif, retries); \
        struct rpc_clnt *c = make_clnt(1, to); \
        struct rpc_task *t = make_task(c, 0); \
        struct rpc_rqst *r = make_req(t, in_jif); \
        ck_assert_uint_eq(get_normal_io_req_timeout(r), expected); \
    } END_TEST

IO_LIN_TEST(io_lin_p1, 1,  5 * HZ, 10 * HZ, 1000 * HZ, 15 * HZ)
IO_LIN_TEST(io_lin_p2, 2,  5 * HZ, 10 * HZ, 1000 * HZ, 20 * HZ)
IO_LIN_TEST(io_lin_p3, 3,  5 * HZ, 10 * HZ, 1000 * HZ, 25 * HZ)
IO_LIN_TEST(io_lin_p4, 5,  5 * HZ, 10 * HZ, 1000 * HZ, 35 * HZ)
IO_LIN_TEST(io_lin_p5, 5, 20 * HZ, 10 * HZ, 1000 * HZ,110 * HZ)
IO_LIN_TEST(io_lin_p6, 5, 20 * HZ, 10 * HZ,   80 * HZ, 80 * HZ)
IO_LIN_TEST(io_lin_p7, 0, 20 * HZ, 10 * HZ, 1000 * HZ, 10 * HZ)
IO_LIN_TEST(io_lin_p8, 1,  1 * HZ,  1 * HZ, 1000 * HZ,  2 * HZ)
IO_LIN_TEST(io_lin_p9, 2,  1 * HZ,  1 * HZ, 1000 * HZ,  3 * HZ)

/* ============================================================ */
/* failover_init_task_req                                       */
/* ============================================================ */

START_TEST(init_req_disabled_no_op) {
    common_setup();
    stub_multipath_state = 0;
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 99);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_timeout, 99);  /* unchanged */
    ck_assert_uint_eq(r->rq_majortimeo, 0);
} END_TEST

START_TEST(init_req_null_clnt_no_op) {
    common_setup();
    struct rpc_task *t = make_task(NULL, 0);
    struct rpc_rqst *r = make_req(t, 99);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_timeout, 99);
} END_TEST

START_TEST(init_req_non_enfs_no_op) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(0, to);  /* cl_enfs = 0 */
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 99);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_timeout, 99);
} END_TEST

START_TEST(init_req_normal_path_sets_rq_timeout_to_multipath) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 99 * HZ);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_timeout, 30 * HZ);  /* multipath wins */
} END_TEST

START_TEST(init_req_probe_path_uses_path_detect_timeout) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_is_test_xprt_task = true;
    stub_path_detect_timeout_secs = 7;
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 99);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_timeout, 7 * HZ);
} END_TEST

START_TEST(init_req_majortimeo_pure_jiffies_when_elapsed_exceeds_timeout) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;
    /* rq_timeout=60*HZ in, retries=0 → get_normal_io_req_timeout
     * returns 60*HZ. current_timeout = 100000ms * HZ/1000 = 100000 jif.
     * 100000 > 60000 → SUT sets rq_majortimeo = jiffies. */
    stub_ktime_ms_delta = 100000;
    jiffies = 4242;
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 60 * HZ);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_majortimeo, 4242);
} END_TEST

START_TEST(init_req_majortimeo_includes_remaining_when_within_window) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_multipath_timeout_secs = 30;
    /* rq_timeout=60*HZ in, retries=0 → timeout = 60*HZ (NOT the
     * multipath value — the SUT computes timeout off the *original*
     * rq_timeout, then overwrites rq_timeout afterward with the
     * multipath value). current_ms_delta=5000 → 5000 jif.
     * rq_majortimeo = (60000 - 5000) + jiffies. */
    stub_ktime_ms_delta = 5000;
    jiffies = 100;
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 60 * HZ);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_majortimeo, (60 * HZ - 5000) + 100);
} END_TEST

START_TEST(init_req_majortimeo_probe_path_uses_path_detect_timeout) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0);
    struct rpc_clnt *c = make_clnt(1, to);
    stub_is_test_xprt_task = true;
    stub_path_detect_timeout_secs = 7;
    stub_ktime_ms_delta = 1000;  /* 1 sec elapsed */
    jiffies = 50;
    struct rpc_task *t = make_task(c, 0);
    struct rpc_rqst *r = make_req(t, 99);
    failover_init_task_req(t, r);
    ck_assert_uint_eq(r->rq_timeout, 7 * HZ);
    ck_assert_uint_eq(r->rq_majortimeo, (7 * HZ - 1000) + 50);
} END_TEST

/* Parametric: many (init_rq_tmo, multipath_secs, elapsed_ms) combos.
 * The SUT logic on the normal path is:
 *
 *    timeout         = get_normal_io_req_timeout(req)        // pre-overwrite
 *    req->rq_timeout = failover_get_mulitipath_timeout(clnt) // overwrite
 *    current         = ms_delta * HZ / MSEC_PER_SEC
 *    if (timeout > current) rq_majortimeo = (timeout - current) + jiffies
 *    else                   rq_majortimeo = jiffies
 *
 * With retries=0 the timeout equals the input rq_timeout, so the
 * test feeds those values directly. */
#define INIT_NORMAL_TEST(name, init_rq, mp_secs, ms_delta_in, jif_in, want_rq, want_major) \
    START_TEST(name) { \
        common_setup(); \
        struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0); \
        struct rpc_clnt *c = make_clnt(1, to); \
        stub_multipath_timeout_secs = (mp_secs); \
        stub_ktime_ms_delta = (ms_delta_in); \
        jiffies = (jif_in); \
        struct rpc_task *t = make_task(c, 0); \
        struct rpc_rqst *r = make_req(t, init_rq); \
        failover_init_task_req(t, r); \
        ck_assert_uint_eq(r->rq_timeout, want_rq); \
        ck_assert_uint_eq(r->rq_majortimeo, want_major); \
    } END_TEST

/* init_rq = 30*HZ. mp_secs=30 → rq becomes 30*HZ; timeout (= initial rq) = 30*HZ. */
INIT_NORMAL_TEST(init_norm_a, 30 * HZ, 30,    0,    0, 30 * HZ, 30 * HZ)
INIT_NORMAL_TEST(init_norm_b, 30 * HZ, 30, 1000,    0, 30 * HZ, 30 * HZ - 1000)
INIT_NORMAL_TEST(init_norm_c, 30 * HZ, 30, 5000,  100, 30 * HZ, 30 * HZ - 5000 + 100)
INIT_NORMAL_TEST(init_norm_d, 30 * HZ, 30,29999,    0, 30 * HZ,  1)
INIT_NORMAL_TEST(init_norm_e, 30 * HZ, 30,30000,   42, 30 * HZ, 42)
INIT_NORMAL_TEST(init_norm_f, 30 * HZ, 30,40000,   42, 30 * HZ, 42)
/* init_rq = 10*HZ. mp_secs=10 → rq becomes 10*HZ; timeout = 10*HZ. */
INIT_NORMAL_TEST(init_norm_g, 10 * HZ, 10,    0,  500, 10 * HZ, 10 * HZ + 500)
INIT_NORMAL_TEST(init_norm_h, 10 * HZ, 10, 5000,  500, 10 * HZ,  5500)
INIT_NORMAL_TEST(init_norm_i, 10 * HZ, 10, 9999,  500, 10 * HZ,   501)
INIT_NORMAL_TEST(init_norm_j, 10 * HZ, 10,10000,  500, 10 * HZ,   500)
INIT_NORMAL_TEST(init_norm_k, 10 * HZ, 10,15000,  500, 10 * HZ,   500)
/* init_rq = 60*HZ. mp_secs=60 → rq stays 60*HZ; timeout = 60*HZ. */
INIT_NORMAL_TEST(init_norm_l, 60 * HZ, 60,    0,    0, 60 * HZ, 60 * HZ)
INIT_NORMAL_TEST(init_norm_m, 60 * HZ, 60,30000, 1234, 60 * HZ, 30 * HZ + 1234)
INIT_NORMAL_TEST(init_norm_n, 60 * HZ, 60,60000, 1234, 60 * HZ, 1234)
INIT_NORMAL_TEST(init_norm_o, 60 * HZ, 60,90000,    1, 60 * HZ, 1)
/* init_rq=60*HZ. mp_secs=0 → rq becomes clnt.initval = 60*HZ; timeout = 60*HZ. */
INIT_NORMAL_TEST(init_norm_p, 60 * HZ,  0,    0,   55, 60 * HZ, 60 * HZ + 55)
INIT_NORMAL_TEST(init_norm_q, 60 * HZ,  0, 5000,   55, 60 * HZ, 60 * HZ - 5000 + 55)
/* Cases where the new rq_timeout differs from the timeout the
 * majortimeo math is computed against. init_rq=10*HZ, mp_secs=30:
 * timeout = 10*HZ (initial), but rq_timeout overwritten to 30*HZ. */
INIT_NORMAL_TEST(init_norm_r, 10 * HZ, 30,    0,   42, 30 * HZ, 10 * HZ + 42)
INIT_NORMAL_TEST(init_norm_s, 10 * HZ, 30, 5000,   42, 30 * HZ, 10 * HZ - 5000 + 42)
INIT_NORMAL_TEST(init_norm_t, 10 * HZ, 30,15000,   42, 30 * HZ, 42) /* current > timeout */

/* Probe-path parametric. */
#define INIT_PROBE_TEST(name, det_secs, ms_delta_in, jif_in, want_rq, want_major) \
    START_TEST(name) { \
        common_setup(); \
        struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 0); \
        struct rpc_clnt *c = make_clnt(1, to); \
        stub_is_test_xprt_task = true; \
        stub_path_detect_timeout_secs = (det_secs); \
        stub_ktime_ms_delta = (ms_delta_in); \
        jiffies = (jif_in); \
        struct rpc_task *t = make_task(c, 0); \
        struct rpc_rqst *r = make_req(t, 99); \
        failover_init_task_req(t, r); \
        ck_assert_uint_eq(r->rq_timeout, want_rq); \
        ck_assert_uint_eq(r->rq_majortimeo, want_major); \
    } END_TEST

INIT_PROBE_TEST(init_probe_a,  5,    0,    0,  5 * HZ,  5 * HZ)
INIT_PROBE_TEST(init_probe_b,  5, 1000,    0,  5 * HZ,  5 * HZ - 1000)
INIT_PROBE_TEST(init_probe_c,  5, 4999,    0,  5 * HZ,  1)
INIT_PROBE_TEST(init_probe_d,  5, 5000,   42,  5 * HZ, 42)
INIT_PROBE_TEST(init_probe_e,  5,10000,   42,  5 * HZ, 42)
INIT_PROBE_TEST(init_probe_f, 30,    0,  100, 30 * HZ, 30 * HZ + 100)
INIT_PROBE_TEST(init_probe_g, 30,15000,  100, 30 * HZ, 15000 + 100)
INIT_PROBE_TEST(init_probe_h, 30,30000,    0, 30 * HZ, 0)

/* ============================================================ */
/* Parent-walk: a v4 child clnt should adopt the parent's
 * cl_enfs flag. failover_is_enfs_clnt walks cl_parent until it
 * finds the root (== self-parent). */
START_TEST(adjust_v4_child_walks_to_enfs_parent) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *parent = make_clnt(1, to);  /* enfs */
    parent->cl_parent = parent;                   /* self-parent = root */
    struct rpc_clnt *child  = make_clnt(0, to);  /* not-enfs */
    child->cl_parent = parent;
    stub_multipath_timeout_secs = 20;
    struct rpc_task *t = make_task(child, 100 * HZ);
    failover_adjust_task_timeout(t, NULL);
    ck_assert_uint_eq(t->tk_timeout, 20 * HZ);
} END_TEST

START_TEST(adjust_v4_child_walks_to_non_enfs_parent) {
    common_setup();
    struct rpc_timeout *to = make_timeout(60 * HZ, 600 * HZ, 1, 0, 5);
    struct rpc_clnt *parent = make_clnt(0, to);
    parent->cl_parent = parent;
    struct rpc_clnt *child  = make_clnt(1, to);
    child->cl_parent = parent;
    stub_multipath_timeout_secs = 20;
    struct rpc_task *t = make_task(child, 100 * HZ);
    failover_adjust_task_timeout(t, NULL);
    /* parent says non-enfs, so the SUT skips. */
    ck_assert_uint_eq(t->tk_timeout, 100 * HZ);
} END_TEST

/* ============================================================ */
/* Suite plumbing                                               */
/* ============================================================ */

static Suite *failover_time_suite(void)
{
    Suite *s = suite_create("failover_time");

    TCase *t1 = tcase_create("get_multipath_timeout");
    tcase_add_test(t1, get_tmo_zero_config_returns_clnt_initval);
    tcase_add_test(t1, get_tmo_smaller_config_wins);
    tcase_add_test(t1, get_tmo_larger_config_clamped_to_clnt);
    tcase_add_test(t1, get_tmo_equal_returns_either);
    suite_add_tcase(s, t1);

    TCase *t2 = tcase_create("get_multipath_timeout_param");
    tcase_add_test(t2, get_tmo_p_0_60);
    tcase_add_test(t2, get_tmo_p_5_60);
    tcase_add_test(t2, get_tmo_p_10_60);
    tcase_add_test(t2, get_tmo_p_30_60);
    tcase_add_test(t2, get_tmo_p_59_60);
    tcase_add_test(t2, get_tmo_p_60_60);
    tcase_add_test(t2, get_tmo_p_61_60);
    tcase_add_test(t2, get_tmo_p_120_60);
    tcase_add_test(t2, get_tmo_p_300_60);
    tcase_add_test(t2, get_tmo_p_1_30);
    tcase_add_test(t2, get_tmo_p_15_30);
    tcase_add_test(t2, get_tmo_p_29_30);
    tcase_add_test(t2, get_tmo_p_30_30);
    tcase_add_test(t2, get_tmo_p_31_30);
    tcase_add_test(t2, get_tmo_p_5_120);
    tcase_add_test(t2, get_tmo_p_119_120);
    tcase_add_test(t2, get_tmo_p_120_120);
    tcase_add_test(t2, get_tmo_p_121_120);
    tcase_add_test(t2, get_tmo_p_240_120);
    tcase_add_test(t2, get_tmo_p_3600_120);
    tcase_add_test(t2, get_tmo_p_2_3);
    tcase_add_test(t2, get_tmo_p_3_3);
    tcase_add_test(t2, get_tmo_p_4_3);
    tcase_add_test(t2, get_tmo_p_0_300);
    tcase_add_test(t2, get_tmo_p_0_1);
    suite_add_tcase(s, t2);

    TCase *t3 = tcase_create("adjust_task_timeout");
    tcase_add_test(t3, adjust_disabled_multipath_is_no_op);
    tcase_add_test(t3, adjust_null_clnt_is_no_op);
    tcase_add_test(t3, adjust_non_enfs_clnt_is_no_op);
    tcase_add_test(t3, adjust_enfs_clamps_when_task_tmo_larger);
    tcase_add_test(t3, adjust_enfs_keeps_smaller_task_tmo);
    tcase_add_test(t3, adjust_zero_task_tmo_takes_multipath);
    tcase_add_test(t3, adj_p_a);
    tcase_add_test(t3, adj_p_b);
    tcase_add_test(t3, adj_p_c);
    tcase_add_test(t3, adj_p_d);
    tcase_add_test(t3, adj_p_e);
    tcase_add_test(t3, adj_p_f);
    tcase_add_test(t3, adj_p_g);
    tcase_add_test(t3, adj_p_h);
    tcase_add_test(t3, adj_p_i);
    tcase_add_test(t3, adj_p_j);
    tcase_add_test(t3, adj_p_k);
    tcase_add_test(t3, adj_p_l);
    tcase_add_test(t3, adj_p_m);
    tcase_add_test(t3, adj_p_n);
    tcase_add_test(t3, adj_p_o);
    tcase_add_test(t3, adj_p_p);
    tcase_add_test(t3, adj_p_q);
    suite_add_tcase(s, t3);

    TCase *t4 = tcase_create("get_normal_io_req_timeout");
    tcase_add_test(t4, io_tmo_exp_zero_retries_returns_input);
    tcase_add_test(t4, io_tmo_exp_one_retry_doubles);
    tcase_add_test(t4, io_tmo_exp_three_retries_x8);
    tcase_add_test(t4, io_tmo_exp_saturates_at_maxval);
    tcase_add_test(t4, io_tmo_linear_zero_retries);
    tcase_add_test(t4, io_tmo_linear_three_retries);
    tcase_add_test(t4, io_tmo_linear_saturates_at_maxval);
    tcase_add_test(t4, io_tmo_overflow_clamped_to_maxval);
    tcase_add_test(t4, io_exp_2x);
    tcase_add_test(t4, io_exp_4x);
    tcase_add_test(t4, io_exp_8x);
    tcase_add_test(t4, io_exp_16x);
    tcase_add_test(t4, io_exp_32x);
    tcase_add_test(t4, io_exp_clamp_64);
    tcase_add_test(t4, io_exp_clamp_128);
    tcase_add_test(t4, io_exp_zero_in_zero);
    tcase_add_test(t4, io_exp_in_8);
    tcase_add_test(t4, io_lin_p1);
    tcase_add_test(t4, io_lin_p2);
    tcase_add_test(t4, io_lin_p3);
    tcase_add_test(t4, io_lin_p4);
    tcase_add_test(t4, io_lin_p5);
    tcase_add_test(t4, io_lin_p6);
    tcase_add_test(t4, io_lin_p7);
    tcase_add_test(t4, io_lin_p8);
    tcase_add_test(t4, io_lin_p9);
    suite_add_tcase(s, t4);

    TCase *t5 = tcase_create("init_task_req");
    tcase_add_test(t5, init_req_disabled_no_op);
    tcase_add_test(t5, init_req_null_clnt_no_op);
    tcase_add_test(t5, init_req_non_enfs_no_op);
    tcase_add_test(t5, init_req_normal_path_sets_rq_timeout_to_multipath);
    tcase_add_test(t5, init_req_probe_path_uses_path_detect_timeout);
    tcase_add_test(t5, init_req_majortimeo_pure_jiffies_when_elapsed_exceeds_timeout);
    tcase_add_test(t5, init_req_majortimeo_includes_remaining_when_within_window);
    tcase_add_test(t5, init_req_majortimeo_probe_path_uses_path_detect_timeout);
    tcase_add_test(t5, init_norm_a);
    tcase_add_test(t5, init_norm_b);
    tcase_add_test(t5, init_norm_c);
    tcase_add_test(t5, init_norm_d);
    tcase_add_test(t5, init_norm_e);
    tcase_add_test(t5, init_norm_f);
    tcase_add_test(t5, init_norm_g);
    tcase_add_test(t5, init_norm_h);
    tcase_add_test(t5, init_norm_i);
    tcase_add_test(t5, init_norm_j);
    tcase_add_test(t5, init_norm_k);
    tcase_add_test(t5, init_norm_l);
    tcase_add_test(t5, init_norm_m);
    tcase_add_test(t5, init_norm_n);
    tcase_add_test(t5, init_norm_o);
    tcase_add_test(t5, init_norm_p);
    tcase_add_test(t5, init_norm_q);
    tcase_add_test(t5, init_norm_r);
    tcase_add_test(t5, init_norm_s);
    tcase_add_test(t5, init_norm_t);
    tcase_add_test(t5, init_probe_a);
    tcase_add_test(t5, init_probe_b);
    tcase_add_test(t5, init_probe_c);
    tcase_add_test(t5, init_probe_d);
    tcase_add_test(t5, init_probe_e);
    tcase_add_test(t5, init_probe_f);
    tcase_add_test(t5, init_probe_g);
    tcase_add_test(t5, init_probe_h);
    suite_add_tcase(s, t5);

    TCase *t6 = tcase_create("v4_parent_walk");
    tcase_add_test(t6, adjust_v4_child_walks_to_enfs_parent);
    tcase_add_test(t6, adjust_v4_child_walks_to_non_enfs_parent);
    suite_add_tcase(s, t6);

    return s;
}

#define CHECK_RUNNER_SUITE  failover_time_suite
#include "check_runner.h"
