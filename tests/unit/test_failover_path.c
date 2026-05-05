/* SPDX-License-Identifier: GPL-2.0 */
/*
 * test_failover_path.c — Check tests for failover_path.c policy
 * decisions.
 *
 * Coverage:
 *   - failover_get_nfs3_retry_policy: every NFSv3 procedure
 *     classified as transactional (RETRY_DELAY) or idempotent
 *     (RETRY); plus the NULL-procinfo error branch
 *   - failover_get_nfs4_retry_policy: same for NFSv4
 *   - failover_get_retry_policy: top-level dispatch — version
 *     selection, RPC_TASK_FIXED short-circuit, RPC_WAS_SENT
 *     override to FAILOVER_RETRY
 *
 * The function under test is `static` in production; the SUT
 * compile rule strips `static` to expose it.
 */
#include <check.h>
#include <stdlib.h>
#include <string.h>

#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/sched.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>

/* The enum is local to failover_path.c (defined inside the .c file).
 * Mirror the values here so tests can reference them. The SUT's
 * compile of failover_path.c uses its own definition; cross-TU
 * comparisons go through int. */
enum {
    SUT_FAILOVER_NOACTION       = 1,
    SUT_FAILOVER_RETRY          = 2,
    SUT_FAILOVER_RETRY_DELAY    = 3,
    SUT_FAILOVER_RETURN_TIMEOUT = 4,
};

/* Forward decls of the SUT functions exposed by `-Dstatic=`. */
int failover_get_nfs3_retry_policy(struct rpc_task *task);
int failover_get_nfs4_retry_policy(struct rpc_task *task);
int failover_get_retry_policy(struct rpc_task *task);

/* ============================================================ */
/* Mock-task scaffolding.                                       */
/* ============================================================ */

static struct rpc_procinfo *make_proc_v3(u32 proc)
{
    struct rpc_procinfo *p = calloc(1, sizeof(*p));
    p->p_proc = proc;
    p->p_name = "test-v3";
    return p;
}
static struct rpc_procinfo *make_proc_v4(u32 statidx)
{
    struct rpc_procinfo *p = calloc(1, sizeof(*p));
    p->p_statidx = statidx;
    p->p_name = "test-v4";
    return p;
}
static struct rpc_clnt *make_clnt(u32 vers)
{
    struct rpc_clnt *c = calloc(1, sizeof(*c));
    c->cl_vers = vers;
    return c;
}
static struct rpc_task *make_task(struct rpc_clnt *clnt,
                                  const struct rpc_procinfo *proc,
                                  unsigned long flags,
                                  bool was_sent)
{
    struct rpc_task *t = calloc(1, sizeof(*t));
    t->tk_client = clnt;
    t->tk_msg.rpc_proc = proc;
    t->tk_flags = flags;
    /* RPC_WAS_SENT looks at bit 5 of tk_runstate per our shim macro. */
    t->tk_runstate = was_sent ? (1UL << 5) : 0;
    return t;
}

/* ============================================================ */
/* NFSv3 per-procedure policy: transactional ops → RETRY_DELAY, */
/* everything else → RETRY. Plus NULL-procinfo error branch.     */
/* ============================================================ */

#define V3_DELAY_TEST(name, proc) \
    START_TEST(name) { \
        struct rpc_procinfo *p = make_proc_v3(proc); \
        struct rpc_task *t = make_task(NULL, p, 0, false); \
        ck_assert_int_eq(failover_get_nfs3_retry_policy(t), \
                         SUT_FAILOVER_RETRY_DELAY); \
    } END_TEST

#define V3_RETRY_TEST(name, proc) \
    START_TEST(name) { \
        struct rpc_procinfo *p = make_proc_v3(proc); \
        struct rpc_task *t = make_task(NULL, p, 0, false); \
        ck_assert_int_eq(failover_get_nfs3_retry_policy(t), \
                         SUT_FAILOVER_RETRY); \
    } END_TEST

/* Transactional ops: WRITE, SETATTR, CREATE, MKDIR, SYMLINK, MKNOD,
 * REMOVE, RMDIR, RENAME, LINK. SUT's switch statement: WRITE +
 * SETATTR + CREATE + MKDIR + REMOVE + RMDIR + SYMLINK + LINK. */
V3_DELAY_TEST(v3_write_is_delay,    NFS3PROC_WRITE)
V3_DELAY_TEST(v3_setattr_is_delay,  NFS3PROC_SETATTR)
V3_DELAY_TEST(v3_create_is_delay,   NFS3PROC_CREATE)
V3_DELAY_TEST(v3_mkdir_is_delay,    NFS3PROC_MKDIR)
V3_DELAY_TEST(v3_remove_is_delay,   NFS3PROC_REMOVE)
V3_DELAY_TEST(v3_rmdir_is_delay,    NFS3PROC_RMDIR)
V3_DELAY_TEST(v3_symlink_is_delay,  NFS3PROC_SYMLINK)
V3_DELAY_TEST(v3_link_is_delay,     NFS3PROC_LINK)

/* Idempotent / read-only ops → RETRY. */
V3_RETRY_TEST(v3_null_is_retry,         NFS3PROC_NULL)
V3_RETRY_TEST(v3_getattr_is_retry,      NFS3PROC_GETATTR)
V3_RETRY_TEST(v3_lookup_is_retry,       NFS3PROC_LOOKUP)
V3_RETRY_TEST(v3_access_is_retry,       NFS3PROC_ACCESS)
V3_RETRY_TEST(v3_readlink_is_retry,     NFS3PROC_READLINK)
V3_RETRY_TEST(v3_read_is_retry,         NFS3PROC_READ)
V3_RETRY_TEST(v3_mknod_is_retry,        NFS3PROC_MKNOD)
V3_RETRY_TEST(v3_rename_is_retry,       NFS3PROC_RENAME)
V3_RETRY_TEST(v3_readdir_is_retry,      NFS3PROC_READDIR)
V3_RETRY_TEST(v3_readdirplus_is_retry,  NFS3PROC_READDIRPLUS)
V3_RETRY_TEST(v3_fsstat_is_retry,       NFS3PROC_FSSTAT)
V3_RETRY_TEST(v3_fsinfo_is_retry,       NFS3PROC_FSINFO)
V3_RETRY_TEST(v3_pathconf_is_retry,     NFS3PROC_PATHCONF)
V3_RETRY_TEST(v3_commit_is_retry,       NFS3PROC_COMMIT)

START_TEST(v3_null_procinfo_returns_NOACTION) {
    struct rpc_task *t = make_task(NULL, NULL, 0, false);
    ck_assert_int_eq(failover_get_nfs3_retry_policy(t), SUT_FAILOVER_NOACTION);
} END_TEST

/* ============================================================ */
/* NFSv4 per-procedure policy: WRITE, SETATTR, CREATE, REMOVE,  */
/* RENAME, LINK, SYMLINK, SETACL → RETRY_DELAY. Others → RETRY. */
/* ============================================================ */

#define V4_DELAY_TEST(name, statidx) \
    START_TEST(name) { \
        struct rpc_procinfo *p = make_proc_v4(statidx); \
        struct rpc_task *t = make_task(NULL, p, 0, false); \
        ck_assert_int_eq(failover_get_nfs4_retry_policy(t), \
                         SUT_FAILOVER_RETRY_DELAY); \
    } END_TEST

#define V4_RETRY_TEST(name, statidx) \
    START_TEST(name) { \
        struct rpc_procinfo *p = make_proc_v4(statidx); \
        struct rpc_task *t = make_task(NULL, p, 0, false); \
        ck_assert_int_eq(failover_get_nfs4_retry_policy(t), \
                         SUT_FAILOVER_RETRY); \
    } END_TEST

V4_DELAY_TEST(v4_write_is_delay,    NFSPROC4_CLNT_WRITE)
V4_DELAY_TEST(v4_setattr_is_delay,  NFSPROC4_CLNT_SETATTR)
V4_DELAY_TEST(v4_create_is_delay,   NFSPROC4_CLNT_CREATE)
V4_DELAY_TEST(v4_remove_is_delay,   NFSPROC4_CLNT_REMOVE)
V4_DELAY_TEST(v4_rename_is_delay,   NFSPROC4_CLNT_RENAME)
V4_DELAY_TEST(v4_link_is_delay,     NFSPROC4_CLNT_LINK)
V4_DELAY_TEST(v4_symlink_is_delay,  NFSPROC4_CLNT_SYMLINK)
V4_DELAY_TEST(v4_setacl_is_delay,   NFSPROC4_CLNT_SETACL)

V4_RETRY_TEST(v4_null_is_retry,        NFSPROC4_CLNT_NULL)
V4_RETRY_TEST(v4_read_is_retry,        NFSPROC4_CLNT_READ)
V4_RETRY_TEST(v4_commit_is_retry,      NFSPROC4_CLNT_COMMIT)
V4_RETRY_TEST(v4_open_is_retry,        NFSPROC4_CLNT_OPEN)
V4_RETRY_TEST(v4_close_is_retry,       NFSPROC4_CLNT_CLOSE)
V4_RETRY_TEST(v4_renew_is_retry,       NFSPROC4_CLNT_RENEW)
V4_RETRY_TEST(v4_lock_is_retry,        NFSPROC4_CLNT_LOCK)
V4_RETRY_TEST(v4_lockt_is_retry,       NFSPROC4_CLNT_LOCKT)
V4_RETRY_TEST(v4_locku_is_retry,       NFSPROC4_CLNT_LOCKU)
V4_RETRY_TEST(v4_access_is_retry,      NFSPROC4_CLNT_ACCESS)
V4_RETRY_TEST(v4_getattr_is_retry,     NFSPROC4_CLNT_GETATTR)
V4_RETRY_TEST(v4_lookup_is_retry,      NFSPROC4_CLNT_LOOKUP)
V4_RETRY_TEST(v4_lookup_root_is_retry, NFSPROC4_CLNT_LOOKUP_ROOT)
V4_RETRY_TEST(v4_pathconf_is_retry,    NFSPROC4_CLNT_PATHCONF)
V4_RETRY_TEST(v4_statfs_is_retry,      NFSPROC4_CLNT_STATFS)
V4_RETRY_TEST(v4_readlink_is_retry,    NFSPROC4_CLNT_READLINK)
V4_RETRY_TEST(v4_readdir_is_retry,     NFSPROC4_CLNT_READDIR)
V4_RETRY_TEST(v4_server_caps_is_retry, NFSPROC4_CLNT_SERVER_CAPS)
V4_RETRY_TEST(v4_delegreturn_is_retry, NFSPROC4_CLNT_DELEGRETURN)
V4_RETRY_TEST(v4_getacl_is_retry,      NFSPROC4_CLNT_GETACL)
V4_RETRY_TEST(v4_fsinfo_is_retry,      NFSPROC4_CLNT_FSINFO)
V4_RETRY_TEST(v4_setclientid_is_retry, NFSPROC4_CLNT_SETCLIENTID)

START_TEST(v4_null_procinfo_returns_NOACTION) {
    struct rpc_task *t = make_task(NULL, NULL, 0, false);
    ck_assert_int_eq(failover_get_nfs4_retry_policy(t), SUT_FAILOVER_NOACTION);
} END_TEST

/* ============================================================ */
/* failover_get_retry_policy top-level dispatch.                */
/* Note: this calls pm_ping_is_test_xprt_task which is in our   */
/* stubs (returns false by default), and uses tk_client->cl_vers */
/* to choose the per-version helper.                             */
/* ============================================================ */

START_TEST(toplevel_v3_write_was_sent_returns_RETRY_DELAY) {
    struct rpc_clnt *c = make_clnt(3);
    struct rpc_procinfo *p = make_proc_v3(NFS3PROC_WRITE);
    struct rpc_task *t = make_task(c, p, 0, /*was_sent*/true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_RETRY_DELAY);
} END_TEST

START_TEST(toplevel_v3_write_not_sent_returns_RETRY) {
    /* Even WRITE collapses to RETRY (no delay) when the task was
     * never put on the wire — there's nothing to bounce against. */
    struct rpc_clnt *c = make_clnt(3);
    struct rpc_procinfo *p = make_proc_v3(NFS3PROC_WRITE);
    struct rpc_task *t = make_task(c, p, 0, /*was_sent*/false);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_RETRY);
} END_TEST

START_TEST(toplevel_v3_read_was_sent_returns_RETRY) {
    struct rpc_clnt *c = make_clnt(3);
    struct rpc_procinfo *p = make_proc_v3(NFS3PROC_READ);
    struct rpc_task *t = make_task(c, p, 0, true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_RETRY);
} END_TEST

START_TEST(toplevel_v4_write_was_sent_returns_RETRY_DELAY) {
    struct rpc_clnt *c = make_clnt(4);
    struct rpc_procinfo *p = make_proc_v4(NFSPROC4_CLNT_WRITE);
    struct rpc_task *t = make_task(c, p, 0, true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_RETRY_DELAY);
} END_TEST

START_TEST(toplevel_v4_read_was_sent_returns_RETRY) {
    struct rpc_clnt *c = make_clnt(4);
    struct rpc_procinfo *p = make_proc_v4(NFSPROC4_CLNT_READ);
    struct rpc_task *t = make_task(c, p, 0, true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_RETRY);
} END_TEST

START_TEST(toplevel_unknown_version_returns_NOACTION) {
    /* SUT only knows v3 and v4 — anything else falls through to
     * NOACTION before consulting the per-version helper. */
    struct rpc_clnt *c = make_clnt(2);
    struct rpc_procinfo *p = make_proc_v3(NFS3PROC_READ);
    struct rpc_task *t = make_task(c, p, 0, false);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_NOACTION);
} END_TEST

START_TEST(toplevel_RPC_TASK_FIXED_short_circuits_to_NOACTION) {
    /* Production code: tasks meant for a specific xprt (the
     * fixed-path flag) must never be re-routed by failover. */
    struct rpc_clnt *c = make_clnt(3);
    struct rpc_procinfo *p = make_proc_v3(NFS3PROC_WRITE);
    struct rpc_task *t = make_task(c, p, RPC_TASK_FIXED, true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_NOACTION);
} END_TEST

START_TEST(toplevel_v3_null_proc_returns_NOACTION) {
    /* tk_msg.rpc_proc == NULL: nfs3_retry_policy returns NOACTION. */
    struct rpc_clnt *c = make_clnt(3);
    struct rpc_task *t = make_task(c, NULL, 0, true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_NOACTION);
} END_TEST

START_TEST(toplevel_v4_null_proc_returns_NOACTION) {
    struct rpc_clnt *c = make_clnt(4);
    struct rpc_task *t = make_task(c, NULL, 0, true);
    ck_assert_int_eq(failover_get_retry_policy(t), SUT_FAILOVER_NOACTION);
} END_TEST

/* Cross-product: every v3 transactional op + every flag combo. */
#define V3_FLAGS_DELAY_TEST(name, proc, was_sent, expected) \
    START_TEST(name) { \
        struct rpc_clnt *c = make_clnt(3); \
        struct rpc_procinfo *p = make_proc_v3(proc); \
        struct rpc_task *t = make_task(c, p, 0, was_sent); \
        ck_assert_int_eq(failover_get_retry_policy(t), expected); \
    } END_TEST

V3_FLAGS_DELAY_TEST(v3_top_write_sent,    NFS3PROC_WRITE,   true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_top_write_unsent,  NFS3PROC_WRITE,   false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_setattr_sent,  NFS3PROC_SETATTR, true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_top_setattr_unsent,NFS3PROC_SETATTR, false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_create_sent,   NFS3PROC_CREATE,  true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_top_create_unsent, NFS3PROC_CREATE,  false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_mkdir_sent,    NFS3PROC_MKDIR,   true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_top_remove_sent,   NFS3PROC_REMOVE,  true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_top_link_sent,     NFS3PROC_LINK,    true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_top_symlink_sent,  NFS3PROC_SYMLINK, true,  SUT_FAILOVER_RETRY_DELAY)

/* ============================================================ */
/* Suite plumbing.                                              */
/* ============================================================ */

static Suite *failover_path_suite(void)
{
    Suite *s = suite_create("failover_path");

    TCase *tc3 = tcase_create("nfs3_per_procedure");
    tcase_add_test(tc3, v3_write_is_delay);
    tcase_add_test(tc3, v3_setattr_is_delay);
    tcase_add_test(tc3, v3_create_is_delay);
    tcase_add_test(tc3, v3_mkdir_is_delay);
    tcase_add_test(tc3, v3_remove_is_delay);
    tcase_add_test(tc3, v3_rmdir_is_delay);
    tcase_add_test(tc3, v3_symlink_is_delay);
    tcase_add_test(tc3, v3_link_is_delay);
    tcase_add_test(tc3, v3_null_is_retry);
    tcase_add_test(tc3, v3_getattr_is_retry);
    tcase_add_test(tc3, v3_lookup_is_retry);
    tcase_add_test(tc3, v3_access_is_retry);
    tcase_add_test(tc3, v3_readlink_is_retry);
    tcase_add_test(tc3, v3_read_is_retry);
    tcase_add_test(tc3, v3_mknod_is_retry);
    tcase_add_test(tc3, v3_rename_is_retry);
    tcase_add_test(tc3, v3_readdir_is_retry);
    tcase_add_test(tc3, v3_readdirplus_is_retry);
    tcase_add_test(tc3, v3_fsstat_is_retry);
    tcase_add_test(tc3, v3_fsinfo_is_retry);
    tcase_add_test(tc3, v3_pathconf_is_retry);
    tcase_add_test(tc3, v3_commit_is_retry);
    tcase_add_test(tc3, v3_null_procinfo_returns_NOACTION);
    suite_add_tcase(s, tc3);

    TCase *tc4 = tcase_create("nfs4_per_procedure");
    tcase_add_test(tc4, v4_write_is_delay);
    tcase_add_test(tc4, v4_setattr_is_delay);
    tcase_add_test(tc4, v4_create_is_delay);
    tcase_add_test(tc4, v4_remove_is_delay);
    tcase_add_test(tc4, v4_rename_is_delay);
    tcase_add_test(tc4, v4_link_is_delay);
    tcase_add_test(tc4, v4_symlink_is_delay);
    tcase_add_test(tc4, v4_setacl_is_delay);
    tcase_add_test(tc4, v4_null_is_retry);
    tcase_add_test(tc4, v4_read_is_retry);
    tcase_add_test(tc4, v4_commit_is_retry);
    tcase_add_test(tc4, v4_open_is_retry);
    tcase_add_test(tc4, v4_close_is_retry);
    tcase_add_test(tc4, v4_renew_is_retry);
    tcase_add_test(tc4, v4_lock_is_retry);
    tcase_add_test(tc4, v4_lockt_is_retry);
    tcase_add_test(tc4, v4_locku_is_retry);
    tcase_add_test(tc4, v4_access_is_retry);
    tcase_add_test(tc4, v4_getattr_is_retry);
    tcase_add_test(tc4, v4_lookup_is_retry);
    tcase_add_test(tc4, v4_lookup_root_is_retry);
    tcase_add_test(tc4, v4_pathconf_is_retry);
    tcase_add_test(tc4, v4_statfs_is_retry);
    tcase_add_test(tc4, v4_readlink_is_retry);
    tcase_add_test(tc4, v4_readdir_is_retry);
    tcase_add_test(tc4, v4_server_caps_is_retry);
    tcase_add_test(tc4, v4_delegreturn_is_retry);
    tcase_add_test(tc4, v4_getacl_is_retry);
    tcase_add_test(tc4, v4_fsinfo_is_retry);
    tcase_add_test(tc4, v4_setclientid_is_retry);
    tcase_add_test(tc4, v4_null_procinfo_returns_NOACTION);
    suite_add_tcase(s, tc4);

    TCase *tct = tcase_create("toplevel_dispatch");
    tcase_add_test(tct, toplevel_v3_write_was_sent_returns_RETRY_DELAY);
    tcase_add_test(tct, toplevel_v3_write_not_sent_returns_RETRY);
    tcase_add_test(tct, toplevel_v3_read_was_sent_returns_RETRY);
    tcase_add_test(tct, toplevel_v4_write_was_sent_returns_RETRY_DELAY);
    tcase_add_test(tct, toplevel_v4_read_was_sent_returns_RETRY);
    tcase_add_test(tct, toplevel_unknown_version_returns_NOACTION);
    tcase_add_test(tct, toplevel_RPC_TASK_FIXED_short_circuits_to_NOACTION);
    tcase_add_test(tct, toplevel_v3_null_proc_returns_NOACTION);
    tcase_add_test(tct, toplevel_v4_null_proc_returns_NOACTION);
    suite_add_tcase(s, tct);

    TCase *tcfx = tcase_create("toplevel_v3_flags_cross");
    tcase_add_test(tcfx, v3_top_write_sent);
    tcase_add_test(tcfx, v3_top_write_unsent);
    tcase_add_test(tcfx, v3_top_setattr_sent);
    tcase_add_test(tcfx, v3_top_setattr_unsent);
    tcase_add_test(tcfx, v3_top_create_sent);
    tcase_add_test(tcfx, v3_top_create_unsent);
    tcase_add_test(tcfx, v3_top_mkdir_sent);
    tcase_add_test(tcfx, v3_top_remove_sent);
    tcase_add_test(tcfx, v3_top_link_sent);
    tcase_add_test(tcfx, v3_top_symlink_sent);
    suite_add_tcase(s, tcfx);

    return s;
}

#define CHECK_RUNNER_SUITE  failover_path_suite
#include "check_runner.h"
