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

/* Cross-product: every v3 idempotent op × {sent, not-sent} →
 * always RETRY (sent doesn't promote idempotent ops to delay). */
V3_FLAGS_DELAY_TEST(v3_top_read_sent,     NFS3PROC_READ,    true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_read_unsent,   NFS3PROC_READ,    false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_getattr_sent,  NFS3PROC_GETATTR, true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_getattr_unsent,NFS3PROC_GETATTR, false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_lookup_sent,   NFS3PROC_LOOKUP,  true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_lookup_unsent, NFS3PROC_LOOKUP,  false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_access_sent,   NFS3PROC_ACCESS,  true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_access_unsent, NFS3PROC_ACCESS,  false, SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_readdir_sent,  NFS3PROC_READDIR, true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_commit_sent,   NFS3PROC_COMMIT,  true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_fsstat_sent,   NFS3PROC_FSSTAT,  true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_fsinfo_sent,   NFS3PROC_FSINFO,  true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_pathconf_sent, NFS3PROC_PATHCONF,true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_readlink_sent, NFS3PROC_READLINK,true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_top_null_sent,     NFS3PROC_NULL,    true,  SUT_FAILOVER_RETRY)

/* These four were renamed mid-iteration — they don't actually pass
 * the RPC_TASK_FIXED flag (that's the V3_FIXED_FLAG_TEST batch
 * below). Without the flag, behaviour is normal: transactional ops
 * sent → DELAY, idempotent → RETRY. */
V3_FLAGS_DELAY_TEST(v3_fixed_write_misnamed,    NFS3PROC_WRITE,   true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_fixed_setattr_misnamed,  NFS3PROC_SETATTR, true,  SUT_FAILOVER_RETRY_DELAY)
V3_FLAGS_DELAY_TEST(v3_fixed_read_misnamed,     NFS3PROC_READ,    true,  SUT_FAILOVER_RETRY)
V3_FLAGS_DELAY_TEST(v3_fixed_getattr_misnamed,  NFS3PROC_GETATTR, true,  SUT_FAILOVER_RETRY)

/* Use a different macro for the FIXED tests to actually pass the flag. */
#undef V3_FLAGS_DELAY_TEST
#define V3_FLAGS_DELAY_TEST(name, proc, was_sent, expected) \
    START_TEST(name) { \
        struct rpc_clnt *c = make_clnt(3); \
        struct rpc_procinfo *p = make_proc_v3(proc); \
        struct rpc_task *t = make_task(c, p, RPC_TASK_FIXED, was_sent); \
        ck_assert_int_eq(failover_get_retry_policy(t), expected); \
    } END_TEST

V3_FLAGS_DELAY_TEST(v3_fixed_flag_write,   NFS3PROC_WRITE,   true,  SUT_FAILOVER_NOACTION)
V3_FLAGS_DELAY_TEST(v3_fixed_flag_setattr, NFS3PROC_SETATTR, true,  SUT_FAILOVER_NOACTION)
V3_FLAGS_DELAY_TEST(v3_fixed_flag_read,    NFS3PROC_READ,    true,  SUT_FAILOVER_NOACTION)
V3_FLAGS_DELAY_TEST(v3_fixed_flag_lookup,  NFS3PROC_LOOKUP,  true,  SUT_FAILOVER_NOACTION)
V3_FLAGS_DELAY_TEST(v3_fixed_flag_create,  NFS3PROC_CREATE,  true,  SUT_FAILOVER_NOACTION)
V3_FLAGS_DELAY_TEST(v3_fixed_flag_remove,  NFS3PROC_REMOVE,  true,  SUT_FAILOVER_NOACTION)
V3_FLAGS_DELAY_TEST(v3_fixed_flag_unsent,  NFS3PROC_WRITE,   false, SUT_FAILOVER_NOACTION)

/* v4 cross-product: idempotent ops never promote to RETRY_DELAY. */
#undef V3_FLAGS_DELAY_TEST
#define V4_FLAGS_TEST(name, proc, was_sent, expected) \
    START_TEST(name) { \
        struct rpc_clnt *c = make_clnt(4); \
        struct rpc_procinfo *p = make_proc_v4(proc); \
        struct rpc_task *t = make_task(c, p, 0, was_sent); \
        ck_assert_int_eq(failover_get_retry_policy(t), expected); \
    } END_TEST

V4_FLAGS_TEST(v4_top_write_sent,     NFSPROC4_CLNT_WRITE,    true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_write_unsent,   NFSPROC4_CLNT_WRITE,    false, SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_setattr_sent,   NFSPROC4_CLNT_SETATTR,  true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_setattr_unsent, NFSPROC4_CLNT_SETATTR,  false, SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_create_sent,    NFSPROC4_CLNT_CREATE,   true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_remove_sent,    NFSPROC4_CLNT_REMOVE,   true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_rename_sent,    NFSPROC4_CLNT_RENAME,   true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_link_sent,      NFSPROC4_CLNT_LINK,     true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_symlink_sent,   NFSPROC4_CLNT_SYMLINK,  true,  SUT_FAILOVER_RETRY_DELAY)
V4_FLAGS_TEST(v4_top_setacl_sent,    NFSPROC4_CLNT_SETACL,   true,  SUT_FAILOVER_RETRY_DELAY)

V4_FLAGS_TEST(v4_top_read_sent,      NFSPROC4_CLNT_READ,     true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_read_unsent,    NFSPROC4_CLNT_READ,     false, SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_getattr_sent,   NFSPROC4_CLNT_GETATTR,  true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_open_sent,      NFSPROC4_CLNT_OPEN,     true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_close_sent,     NFSPROC4_CLNT_CLOSE,    true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_lock_sent,      NFSPROC4_CLNT_LOCK,     true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_locku_sent,     NFSPROC4_CLNT_LOCKU,    true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_renew_sent,     NFSPROC4_CLNT_RENEW,    true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_commit_sent,    NFSPROC4_CLNT_COMMIT,   true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_lookup_sent,    NFSPROC4_CLNT_LOOKUP,   true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_access_sent,    NFSPROC4_CLNT_ACCESS,   true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_statfs_sent,    NFSPROC4_CLNT_STATFS,   true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_readdir_sent,   NFSPROC4_CLNT_READDIR,  true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_readlink_sent,  NFSPROC4_CLNT_READLINK, true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_pathconf_sent,  NFSPROC4_CLNT_PATHCONF, true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_getacl_sent,    NFSPROC4_CLNT_GETACL,   true,  SUT_FAILOVER_RETRY)
V4_FLAGS_TEST(v4_top_delegreturn,    NFSPROC4_CLNT_DELEGRETURN, true, SUT_FAILOVER_RETRY)

/* ============================================================ */
/* failover_check_task — gatekeeper for the failover entry      */
/* points. Returns 0 on "this task is eligible for failover     */
/* handling", -EINVAL otherwise.                                */
/* ============================================================ */

int failover_check_task(struct rpc_task *task);
bool failover_is_task_use_fixed_path(struct rpc_task *task);
bool failover_task_need_call_start_again(struct rpc_task *task);
bool failover_prepare_transmit(struct rpc_task *task);

/* The stub for enfs_get_config_multipath_state lives in
 * failover_path_stubs.c; default = 1 (enabled). The stub for
 * pm_get_path_state returns NORMAL by default. */
extern int32_t enfs_get_config_multipath_state(void);

/* For tests that need to flip the multipath state on/off we re-stub
 * via a dedicated weak symbol — the existing failover_path_stubs.c
 * has a hardcoded `return 1;`. The helper macro below works around
 * that by toggling task fields the SUT reads instead. So most
 * "disabled multipath" coverage comes via failover_time_tests; here
 * we focus on the other branches. */

START_TEST(check_task_NULL_task_returns_EINVAL) {
    /* unlikely(task == NULL) branch; SUT reads task->tk_client only
     * AFTER the unlikely check. */
    ck_assert_int_eq(failover_check_task(NULL), -EINVAL);
} END_TEST

START_TEST(check_task_NULL_clnt_returns_EINVAL) {
    struct rpc_task *t = make_task(NULL, NULL, 0, false);
    ck_assert_int_eq(failover_check_task(t), -EINVAL);
} END_TEST

START_TEST(check_task_wrong_program_returns_EINVAL) {
    struct rpc_clnt *c = make_clnt(3);
    c->cl_prog = 999999;  /* not NFS_PROGRAM */
    c->cl_enfs = 1;
    c->cl_parent = c;     /* self-parent = root */
    struct rpc_task *t = make_task(c, NULL, 0, false);
    ck_assert_int_eq(failover_check_task(t), -EINVAL);
} END_TEST

START_TEST(check_task_non_enfs_clnt_returns_EINVAL) {
    struct rpc_clnt *c = make_clnt(3);
    c->cl_prog = NFS_PROGRAM;
    c->cl_enfs = 0;       /* not enfs-managed */
    c->cl_parent = c;
    struct rpc_task *t = make_task(c, NULL, 0, false);
    ck_assert_int_eq(failover_check_task(t), -EINVAL);
} END_TEST

START_TEST(check_task_enfs_managed_returns_zero) {
    struct rpc_clnt *c = make_clnt(3);
    c->cl_prog = NFS_PROGRAM;
    c->cl_enfs = 1;
    c->cl_parent = c;
    struct rpc_task *t = make_task(c, NULL, 0, false);
    ck_assert_int_eq(failover_check_task(t), 0);
} END_TEST

START_TEST(check_task_v4_child_walks_to_enfs_parent_returns_zero) {
    struct rpc_clnt *parent = make_clnt(4);
    parent->cl_prog = NFS_PROGRAM;
    parent->cl_enfs = 1;
    parent->cl_parent = parent;
    struct rpc_clnt *child = make_clnt(4);
    child->cl_prog = NFS_PROGRAM;
    child->cl_enfs = 0;       /* child says no */
    child->cl_parent = parent;
    struct rpc_task *t = make_task(child, NULL, 0, false);
    /* Parent walk should pick up the parent's cl_enfs = 1. */
    ck_assert_int_eq(failover_check_task(t), 0);
} END_TEST

/* ============================================================ */
/* failover_is_task_use_fixed_path                              */
/* ============================================================ */

START_TEST(is_fixed_RPC_TASK_FIXED_set_returns_true) {
    struct rpc_task *t = make_task(NULL, NULL, RPC_TASK_FIXED, false);
    ck_assert(failover_is_task_use_fixed_path(t));
} END_TEST

START_TEST(is_fixed_no_FIXED_no_test_xprt_returns_false) {
    struct rpc_task *t = make_task(NULL, NULL, 0, false);
    ck_assert(!failover_is_task_use_fixed_path(t));
} END_TEST

/* RPC_TASK_FIXED + various other flags still triggers fixed-path. */
START_TEST(is_fixed_FIXED_plus_other_flags_still_true) {
    struct rpc_task *t = make_task(NULL, NULL,
                                    RPC_TASK_FIXED | RPC_TASK_ASYNC | RPC_TASK_SOFT,
                                    false);
    ck_assert(failover_is_task_use_fixed_path(t));
} END_TEST

/* ============================================================ */
/* failover_task_need_call_start_again — composes check_task    */
/* with is_task_use_fixed_path.                                 */
/* ============================================================ */

START_TEST(start_again_check_fails_returns_false) {
    /* Non-enfs clnt → check_task returns EINVAL → false. */
    struct rpc_clnt *c = make_clnt(3);
    c->cl_prog = NFS_PROGRAM;
    c->cl_enfs = 0;
    c->cl_parent = c;
    struct rpc_task *t = make_task(c, NULL, 0, false);
    ck_assert(!failover_task_need_call_start_again(t));
} END_TEST

START_TEST(start_again_fixed_returns_false) {
    /* Eligible task BUT RPC_TASK_FIXED → false. */
    struct rpc_clnt *c = make_clnt(3);
    c->cl_prog = NFS_PROGRAM;
    c->cl_enfs = 1;
    c->cl_parent = c;
    struct rpc_task *t = make_task(c, NULL, RPC_TASK_FIXED, false);
    ck_assert(!failover_task_need_call_start_again(t));
} END_TEST

START_TEST(start_again_eligible_returns_true) {
    struct rpc_clnt *c = make_clnt(3);
    c->cl_prog = NFS_PROGRAM;
    c->cl_enfs = 1;
    c->cl_parent = c;
    struct rpc_task *t = make_task(c, NULL, 0, false);
    ck_assert(failover_task_need_call_start_again(t));
} END_TEST

START_TEST(start_again_NULL_task_returns_false) {
    ck_assert(!failover_task_need_call_start_again(NULL));
} END_TEST

START_TEST(start_again_NULL_clnt_returns_false) {
    struct rpc_task *t = make_task(NULL, NULL, 0, false);
    ck_assert(!failover_task_need_call_start_again(t));
} END_TEST

/* ============================================================ */
/* failover_prepare_transmit                                    */
/* ============================================================ */
/* The function delegates: for fixed-path tasks always returns
 * true; otherwise checks pm_get_path_state == FAULT and rejects
 * if so. The stub returns PM_STATE_NORMAL by default, so the
 * second branch returns true unless we manipulate the xprt.
 * Since our stub is hard-coded to NORMAL we can only test the
 * fixed-path and no-FAULT paths here without deeper plumbing. */

START_TEST(prepare_transmit_fixed_path_returns_true) {
    struct rpc_task *t = make_task(NULL, NULL, RPC_TASK_FIXED, false);
    ck_assert(failover_prepare_transmit(t));
} END_TEST

START_TEST(prepare_transmit_normal_path_returns_true) {
    /* Not fixed — delegates to pm_get_path_state which the stub
     * returns NORMAL for. */
    struct rpc_task *t = make_task(NULL, NULL, 0, false);
    ck_assert(failover_prepare_transmit(t));
} END_TEST

/* Parametric: every flag combination → fixed-path predicate. */
#define IS_FIXED_TEST(name, flags, expected) \
    START_TEST(name) { \
        struct rpc_task *t = make_task(NULL, NULL, (flags), false); \
        ck_assert_int_eq((int)failover_is_task_use_fixed_path(t), (expected) ? 1 : 0); \
    } END_TEST

IS_FIXED_TEST(is_fixed_p_no_flags,                    0,                                                 false)
IS_FIXED_TEST(is_fixed_p_FIXED_only,                  RPC_TASK_FIXED,                                    true)
IS_FIXED_TEST(is_fixed_p_FIXED_ASYNC,                 RPC_TASK_FIXED | RPC_TASK_ASYNC,                   true)
IS_FIXED_TEST(is_fixed_p_FIXED_SOFT,                  RPC_TASK_FIXED | RPC_TASK_SOFT,                    true)
IS_FIXED_TEST(is_fixed_p_FIXED_NULLCREDS,             RPC_TASK_FIXED | RPC_TASK_NULLCREDS,               true)
IS_FIXED_TEST(is_fixed_p_FIXED_SENT,                  RPC_TASK_FIXED | RPC_TASK_SENT,                    true)
IS_FIXED_TEST(is_fixed_p_FIXED_all,
              RPC_TASK_FIXED | RPC_TASK_ASYNC | RPC_TASK_SOFT |
              RPC_TASK_NULLCREDS | RPC_TASK_SENT,                                                        true)
IS_FIXED_TEST(is_fixed_p_ASYNC_only,                  RPC_TASK_ASYNC,                                    false)
IS_FIXED_TEST(is_fixed_p_SOFT_only,                   RPC_TASK_SOFT,                                     false)
IS_FIXED_TEST(is_fixed_p_NULLCREDS_only,              RPC_TASK_NULLCREDS,                                false)
IS_FIXED_TEST(is_fixed_p_SENT_only,                   RPC_TASK_SENT,                                     false)
IS_FIXED_TEST(is_fixed_p_ASYNC_SOFT,                  RPC_TASK_ASYNC | RPC_TASK_SOFT,                    false)
IS_FIXED_TEST(is_fixed_p_ASYNC_SOFT_NULLCREDS,        RPC_TASK_ASYNC | RPC_TASK_SOFT | RPC_TASK_NULLCREDS, false)

/* Cross-product: (cl_enfs, cl_prog, RPC_TASK_FIXED) → start_again. */
#define START_AGAIN_TEST(name, prog, enfs_flag, flags, expected) \
    START_TEST(name) { \
        struct rpc_clnt *c = make_clnt(3); \
        c->cl_prog = (prog); \
        c->cl_enfs = (enfs_flag); \
        c->cl_parent = c; \
        struct rpc_task *t = make_task(c, NULL, (flags), false); \
        ck_assert_int_eq((int)failover_task_need_call_start_again(t), \
                          (expected) ? 1 : 0); \
    } END_TEST

START_AGAIN_TEST(start_again_p_aaa, NFS_PROGRAM,    1, 0,              true)
START_AGAIN_TEST(start_again_p_bbb, NFS_PROGRAM,    1, RPC_TASK_FIXED, false)
START_AGAIN_TEST(start_again_p_ccc, NFS_PROGRAM,    0, 0,              false)
START_AGAIN_TEST(start_again_p_ddd, NFS_PROGRAM,    0, RPC_TASK_FIXED, false)
START_AGAIN_TEST(start_again_p_eee, 999999,         1, 0,              false)
START_AGAIN_TEST(start_again_p_fff, 999999,         1, RPC_TASK_FIXED, false)
START_AGAIN_TEST(start_again_p_ggg, NFS_PROGRAM,    1, RPC_TASK_ASYNC, true)
START_AGAIN_TEST(start_again_p_hhh, NFS_PROGRAM,    1, RPC_TASK_SOFT,  true)
START_AGAIN_TEST(start_again_p_iii, NFS_PROGRAM,    1, RPC_TASK_NULLCREDS, true)
START_AGAIN_TEST(start_again_p_jjj, NFS_PROGRAM,    1, RPC_TASK_SENT,  true)
START_AGAIN_TEST(start_again_p_kkk, NFS_PROGRAM,    1,
                  RPC_TASK_FIXED | RPC_TASK_ASYNC | RPC_TASK_SOFT,         false)

/* Cross-product: (cl_prog, cl_enfs) → check_task return code. */
#define CHECK_TASK_TEST(name, prog, enfs_flag, expected) \
    START_TEST(name) { \
        struct rpc_clnt *c = make_clnt(3); \
        c->cl_prog = (prog); \
        c->cl_enfs = (enfs_flag); \
        c->cl_parent = c; \
        struct rpc_task *t = make_task(c, NULL, 0, false); \
        ck_assert_int_eq(failover_check_task(t), (expected)); \
    } END_TEST

CHECK_TASK_TEST(check_task_p_aaa, NFS_PROGRAM, 1, 0)
CHECK_TASK_TEST(check_task_p_bbb, NFS_PROGRAM, 0, -EINVAL)
CHECK_TASK_TEST(check_task_p_ccc, 999999,      1, -EINVAL)
CHECK_TASK_TEST(check_task_p_ddd, 999999,      0, -EINVAL)
CHECK_TASK_TEST(check_task_p_eee, NFS_PROGRAM + 1, 1, -EINVAL)
CHECK_TASK_TEST(check_task_p_fff, 0,           1, -EINVAL)

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
    tcase_add_test(tcfx, v3_top_read_sent);
    tcase_add_test(tcfx, v3_top_read_unsent);
    tcase_add_test(tcfx, v3_top_getattr_sent);
    tcase_add_test(tcfx, v3_top_getattr_unsent);
    tcase_add_test(tcfx, v3_top_lookup_sent);
    tcase_add_test(tcfx, v3_top_lookup_unsent);
    tcase_add_test(tcfx, v3_top_access_sent);
    tcase_add_test(tcfx, v3_top_access_unsent);
    tcase_add_test(tcfx, v3_top_readdir_sent);
    tcase_add_test(tcfx, v3_top_commit_sent);
    tcase_add_test(tcfx, v3_top_fsstat_sent);
    tcase_add_test(tcfx, v3_top_fsinfo_sent);
    tcase_add_test(tcfx, v3_top_pathconf_sent);
    tcase_add_test(tcfx, v3_top_readlink_sent);
    tcase_add_test(tcfx, v3_top_null_sent);
    tcase_add_test(tcfx, v3_fixed_write_misnamed);
    tcase_add_test(tcfx, v3_fixed_setattr_misnamed);
    tcase_add_test(tcfx, v3_fixed_read_misnamed);
    tcase_add_test(tcfx, v3_fixed_getattr_misnamed);
    suite_add_tcase(s, tcfx);

    /* RPC_TASK_FIXED cross-product. */
    TCase *tcfix = tcase_create("toplevel_v3_fixed_flag");
    tcase_add_test(tcfix, v3_fixed_flag_write);
    tcase_add_test(tcfix, v3_fixed_flag_setattr);
    tcase_add_test(tcfix, v3_fixed_flag_read);
    tcase_add_test(tcfix, v3_fixed_flag_lookup);
    tcase_add_test(tcfix, v3_fixed_flag_create);
    tcase_add_test(tcfix, v3_fixed_flag_remove);
    tcase_add_test(tcfix, v3_fixed_flag_unsent);
    suite_add_tcase(s, tcfix);

    /* v4 toplevel-dispatch cross-product. */
    TCase *tcv4x = tcase_create("toplevel_v4_cross");
    tcase_add_test(tcv4x, v4_top_write_sent);
    tcase_add_test(tcv4x, v4_top_write_unsent);
    tcase_add_test(tcv4x, v4_top_setattr_sent);
    tcase_add_test(tcv4x, v4_top_setattr_unsent);
    tcase_add_test(tcv4x, v4_top_create_sent);
    tcase_add_test(tcv4x, v4_top_remove_sent);
    tcase_add_test(tcv4x, v4_top_rename_sent);
    tcase_add_test(tcv4x, v4_top_link_sent);
    tcase_add_test(tcv4x, v4_top_symlink_sent);
    tcase_add_test(tcv4x, v4_top_setacl_sent);
    tcase_add_test(tcv4x, v4_top_read_sent);
    tcase_add_test(tcv4x, v4_top_read_unsent);
    tcase_add_test(tcv4x, v4_top_getattr_sent);
    tcase_add_test(tcv4x, v4_top_open_sent);
    tcase_add_test(tcv4x, v4_top_close_sent);
    tcase_add_test(tcv4x, v4_top_lock_sent);
    tcase_add_test(tcv4x, v4_top_locku_sent);
    tcase_add_test(tcv4x, v4_top_renew_sent);
    tcase_add_test(tcv4x, v4_top_commit_sent);
    tcase_add_test(tcv4x, v4_top_lookup_sent);
    tcase_add_test(tcv4x, v4_top_access_sent);
    tcase_add_test(tcv4x, v4_top_statfs_sent);
    tcase_add_test(tcv4x, v4_top_readdir_sent);
    tcase_add_test(tcv4x, v4_top_readlink_sent);
    tcase_add_test(tcv4x, v4_top_pathconf_sent);
    tcase_add_test(tcv4x, v4_top_getacl_sent);
    tcase_add_test(tcv4x, v4_top_delegreturn);
    suite_add_tcase(s, tcv4x);

    TCase *tcck = tcase_create("check_task");
    tcase_add_test(tcck, check_task_NULL_task_returns_EINVAL);
    tcase_add_test(tcck, check_task_NULL_clnt_returns_EINVAL);
    tcase_add_test(tcck, check_task_wrong_program_returns_EINVAL);
    tcase_add_test(tcck, check_task_non_enfs_clnt_returns_EINVAL);
    tcase_add_test(tcck, check_task_enfs_managed_returns_zero);
    tcase_add_test(tcck, check_task_v4_child_walks_to_enfs_parent_returns_zero);
    tcase_add_test(tcck, check_task_p_aaa);
    tcase_add_test(tcck, check_task_p_bbb);
    tcase_add_test(tcck, check_task_p_ccc);
    tcase_add_test(tcck, check_task_p_ddd);
    tcase_add_test(tcck, check_task_p_eee);
    tcase_add_test(tcck, check_task_p_fff);
    suite_add_tcase(s, tcck);

    TCase *tcfp = tcase_create("is_task_use_fixed_path");
    tcase_add_test(tcfp, is_fixed_RPC_TASK_FIXED_set_returns_true);
    tcase_add_test(tcfp, is_fixed_no_FIXED_no_test_xprt_returns_false);
    tcase_add_test(tcfp, is_fixed_FIXED_plus_other_flags_still_true);
    tcase_add_test(tcfp, is_fixed_p_no_flags);
    tcase_add_test(tcfp, is_fixed_p_FIXED_only);
    tcase_add_test(tcfp, is_fixed_p_FIXED_ASYNC);
    tcase_add_test(tcfp, is_fixed_p_FIXED_SOFT);
    tcase_add_test(tcfp, is_fixed_p_FIXED_NULLCREDS);
    tcase_add_test(tcfp, is_fixed_p_FIXED_SENT);
    tcase_add_test(tcfp, is_fixed_p_FIXED_all);
    tcase_add_test(tcfp, is_fixed_p_ASYNC_only);
    tcase_add_test(tcfp, is_fixed_p_SOFT_only);
    tcase_add_test(tcfp, is_fixed_p_NULLCREDS_only);
    tcase_add_test(tcfp, is_fixed_p_SENT_only);
    tcase_add_test(tcfp, is_fixed_p_ASYNC_SOFT);
    tcase_add_test(tcfp, is_fixed_p_ASYNC_SOFT_NULLCREDS);
    suite_add_tcase(s, tcfp);

    TCase *tcsa = tcase_create("task_need_call_start_again");
    tcase_add_test(tcsa, start_again_check_fails_returns_false);
    tcase_add_test(tcsa, start_again_fixed_returns_false);
    tcase_add_test(tcsa, start_again_eligible_returns_true);
    tcase_add_test(tcsa, start_again_NULL_task_returns_false);
    tcase_add_test(tcsa, start_again_NULL_clnt_returns_false);
    tcase_add_test(tcsa, start_again_p_aaa);
    tcase_add_test(tcsa, start_again_p_bbb);
    tcase_add_test(tcsa, start_again_p_ccc);
    tcase_add_test(tcsa, start_again_p_ddd);
    tcase_add_test(tcsa, start_again_p_eee);
    tcase_add_test(tcsa, start_again_p_fff);
    tcase_add_test(tcsa, start_again_p_ggg);
    tcase_add_test(tcsa, start_again_p_hhh);
    tcase_add_test(tcsa, start_again_p_iii);
    tcase_add_test(tcsa, start_again_p_jjj);
    tcase_add_test(tcsa, start_again_p_kkk);
    suite_add_tcase(s, tcsa);

    TCase *tcpt = tcase_create("prepare_transmit");
    tcase_add_test(tcpt, prepare_transmit_fixed_path_returns_true);
    tcase_add_test(tcpt, prepare_transmit_normal_path_returns_true);
    suite_add_tcase(s, tcpt);

    return s;
}

#define CHECK_RUNNER_SUITE  failover_path_suite
#include "check_runner.h"
