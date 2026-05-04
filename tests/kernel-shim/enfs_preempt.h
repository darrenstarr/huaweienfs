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
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/printk.h>
/* Production NFS headers (preempted as empty stubs above) normally
 * pull errno / slab / printk transitively. Pull them in directly
 * here so source files don't need to know which headers we faked. */
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
#define ENFS_LOG_H
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
int32_t enfs_get_config_dns_auto_multipath_resolution(void);
int32_t enfs_get_config_dns_update_interval(void);
int32_t enfs_get_create_path_no_route(void);
int32_t enfs_get_config_link_count_total(void);
int32_t enfs_get_config_link_count_per_mount(void);
int     enfs_link_count_num(void);
int     enfs_mount_count(void);
bool    enfs_check_config_wwn(uint64_t wwn);
bool    enfs_whitelist_filte(char *ip);
int     GetEnfsConfigIpFiltersCount(void);

/* From enfs.h: link/mount caps */
#define ENFS_MAX_LINK_COUNT          16384
#define DEFAULT_ENFS_MAX_LINK_COUNT  512
#define MIN_ENFS_MAX_LINK_COUNT      512
#define ENFS_MAX_MOUNT_COUNT         256

/* Kernel internal in4_pton / in6_pton (called by is_valid_ip_address
 * in enfs_multipath_parse.c). Provided in tests/stubs/rpc_addr_stubs.c. */
int in4_pton(const char *src, int srclen, unsigned char *dst, int delim,
             const char **end);
int in6_pton(const char *src, int srclen, unsigned char *dst, int delim,
             const char **end);

/* From pm_state.h */
enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt);

/* From enfs_proc.h */
void enfs_iter_rpc_clnt(int (*fn)(struct rpc_clnt *clnt, void *data),
                        void *data);

/* ---- Below: types and helpers that enfs_multipath_parse.c uses
 * (added when extending the test suite to cover that module). ---- */

/* From enfs.h: ip-list and DNS-info types. */
#include <sys/socket.h>     /* for sockaddr_storage */

#define MAX_SUPPORTED_LOCAL_IP_COUNT      8
#define MAX_SUPPORTED_REMOTE_IP_COUNT     1024
#define MIN_SUPPORTED_REMOTE_IP_COUNT     2
#define DEFAULT_SUPPORTED_REMOTE_IP_COUNT 32
#define MAX_DNS_NAME_LEN                  512
#define MAX_DNS_SUPPORTED                 2
#define EXTEND_MAX_DNS_NAME_LEN           256
#define ENFS_NOT_SUPPORT                  524

struct nfs_ip_list {
    int                     count;
    struct sockaddr_storage address[MAX_SUPPORTED_REMOTE_IP_COUNT];
    size_t                  addrlen[MAX_SUPPORTED_REMOTE_IP_COUNT];
};

struct enfs_dns_info_single {
    char dnsname[MAX_DNS_NAME_LEN];
};

struct enfs_route_dns_info {
    int dnsNameCount;
    struct enfs_dns_info_single routeRemoteDnsList[MAX_DNS_SUPPORTED];
};

/* From enfs_log.h: production wraps pr_info/pr_err. We route through
 * the existing pr_* shim macros (which print to stderr), keeping the
 * "enfs:[funcname]" prefix the production format uses. */
#define enfs_log_info(fmt, ...)  pr_info("enfs:[%s]" fmt, __func__, ##__VA_ARGS__)
#define enfs_log_error(fmt, ...) pr_err("enfs:[%s]"  fmt, __func__, ##__VA_ARGS__)
#define enfs_log_debug(fmt, ...) ((void)0)

/* From enfs_adapter.h: the option enum used by parse_options dispatch. */
enum nfsmultipathoptions {
    REMOTEADDR,
    LOCALADDR,
    REMOTEDNSNAME,
    REMOUNTREMOTEADDR,
    REMOUNTLOCALADDR,
    INVALID_OPTION
};

#endif /* ENFS_TESTS_ENFS_PREEMPT_H */
