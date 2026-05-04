/* SPDX-License-Identifier: GPL-2.0 */
/*
 * enfs_preempt.h — force-included before every test compile.
 *
 * The "preempt include-guards" trick: by defining the include guards
 * for enfs internal headers before the source file gets to #include
 * them, we make those headers expand to nothing. We then provide our
 * own minimal definitions of the symbols the source actually needs.
 *
 * Why: the production enfs headers transitively pull in the entire
 * Linux NFS world (linux/nfs.h, linux/nfs_fs_sb.h, linux/parser.h,
 * fs/nfs/internal.h, ...). Faking all of that is days of work and
 * adds zero test value for pure selection-logic tests. Preempting
 * lets enfs_roundrobin.c compile in userspace with ~100 lines of
 * minimal substitutes.
 *
 * If a future test genuinely needs a struct that enfs.h defines, add
 * the field here rather than letting the production header in.
 */
#ifndef ENFS_TESTS_ENFS_PREEMPT_H
#define ENFS_TESTS_ENFS_PREEMPT_H

#include <linux/types.h>
#include <linux/atomic.h>
#include <linux/list.h>
/* enfs_roundrobin.c uses rcu_dereference / rcu_read_lock without
 * including <linux/rcupdate.h> directly — the kernel pulls it in
 * transitively. Pull it in here so source files compile cleanly. */
#include <linux/rcupdate.h>

/* --- Block production headers via include-guard preemption. ------- */
/* enfs internal */
#define _ENFS_H_
#define ENFS_CONFIG_H
#define PM_STATE_H
#define ENFS_PROC_H
#define _NFS_ADAPTER_H_
/* sunrpc adapter (different file, also pulled in transitively) */
#define _SUNRPC_ENFS_ADAPTER_H_
/* The kernel NFS world that enfs.h drags in */
#define _LINUX_NFS_H
#define _LINUX_NFS2_H
#define _LINUX_NFS3_H
#define _LINUX_NFS4_H
#define _LINUX_NFS_FS_H
#define _LINUX_NFS_FS_SB_H
#define _LINUX_PARSER_H
/* fs/nfs/internal.h has no canonical guard — its name varies; we
 * rely on the source not including it directly from enfs/*.c (it
 * doesn't, as of this writing). */

/* --- Minimal substitutes for things the source under test uses. -- */

/* From pm_state.h */
enum enfs_path_state {
    PM_STATE_INIT,
    PM_STATE_NORMAL,
    PM_STATE_UNSTABLE,
    PM_STATE_FAULT,
    PM_STATE_UNDEFINED,
};

static inline bool enfs_is_path_connected(enum enfs_path_state state) {
    return state == PM_STATE_NORMAL || state == PM_STATE_UNSTABLE;
}

/* Forward decl for things below */
struct rpc_xprt;
struct rpc_clnt;
struct rpc_xprt_switch;
struct rpc_task;

/* From enfs.h: the per-xprt context that the selection logic reads
 * queuelen from. Tests construct these with controlled values. */
struct enfs_xprt_context {
    atomic_long_t queuelen;
    bool          main;
    /* Tests don't currently inspect any other field. Extend if
     * needed. */
};

/* From enfs.h: inline in production, but with the production header
 * preempted we declare it here and define it in the stubs (where it
 * can use the test's mocked xprt_get_reserve_context()). */
bool enfs_is_main_xprt(struct rpc_xprt *xprt);

/* From sunrpc_enfs_adapter.h — must return a pointer to the
 * enfs_xprt_context the test attached to this xprt. Implemented in
 * tests/stubs/enfs_deps_stubs.c by lookup in a per-test table. */
void *xprt_get_reserve_context(struct rpc_xprt *xprt);
void  xprt_set_reserve_context(struct rpc_xprt *xprt, void *context);

/* From enfs_config.h */
int32_t enfs_get_config_multipath_state(void);
int32_t enfs_get_native_link_io_status(void);

/* From pm_state.h */
enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt);

/* From enfs_proc.h */
void enfs_iter_rpc_clnt(int (*fn)(struct rpc_clnt *clnt, void *data),
                        void *data);

#endif /* ENFS_TESTS_ENFS_PREEMPT_H */
