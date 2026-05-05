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

/* From pm_state.h — declarations so SUTs that call these (eg
 * failover_path.c calls pm_set_path_state) compile. The stubs
 * provide the implementation. */
struct rpc_xprt;
enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt);
void pm_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state state);
void pm_get_path_state_desc(struct rpc_xprt *xprt, char *buf, int len);
void pm_get_xprt_state_desc(struct rpc_xprt *xprt, char *buf, int len);

/* RPC program numbers — stand-ins for what <linux/nfs.h> et al would
 * provide. Preempted include guards prevent the real headers from
 * defining these. */
#ifndef NFS_PROGRAM
#define NFS_PROGRAM     100003
#endif
#ifndef NFS3_VERSION
#define NFS3_VERSION    3
#endif
#ifndef NFS4_MINOR_VERSION
#define NFS4_MINOR_VERSION 0
#endif

/* NFSv4 client-side procedure stat indices, used by
 * failover_path.c's switch statement. Real values in
 * <linux/nfs4.h> (preempted). */
enum {
    NFSPROC4_CLNT_NULL = 0,
    NFSPROC4_CLNT_READ,
    NFSPROC4_CLNT_WRITE,
    NFSPROC4_CLNT_COMMIT,
    NFSPROC4_CLNT_OPEN,
    NFSPROC4_CLNT_OPEN_CONFIRM,
    NFSPROC4_CLNT_OPEN_NOATTR,
    NFSPROC4_CLNT_OPEN_DOWNGRADE,
    NFSPROC4_CLNT_CLOSE,
    NFSPROC4_CLNT_SETATTR,
    NFSPROC4_CLNT_FSINFO,
    NFSPROC4_CLNT_RENEW,
    NFSPROC4_CLNT_SETCLIENTID,
    NFSPROC4_CLNT_SETCLIENTID_CONFIRM,
    NFSPROC4_CLNT_LOCK,
    NFSPROC4_CLNT_LOCKT,
    NFSPROC4_CLNT_LOCKU,
    NFSPROC4_CLNT_ACCESS,
    NFSPROC4_CLNT_GETATTR,
    NFSPROC4_CLNT_LOOKUP,
    NFSPROC4_CLNT_LOOKUP_ROOT,
    NFSPROC4_CLNT_REMOVE,
    NFSPROC4_CLNT_RENAME,
    NFSPROC4_CLNT_LINK,
    NFSPROC4_CLNT_SYMLINK,
    NFSPROC4_CLNT_CREATE,
    NFSPROC4_CLNT_PATHCONF,
    NFSPROC4_CLNT_STATFS,
    NFSPROC4_CLNT_READLINK,
    NFSPROC4_CLNT_READDIR,
    NFSPROC4_CLNT_SERVER_CAPS,
    NFSPROC4_CLNT_DELEGRETURN,
    NFSPROC4_CLNT_GETACL,
    NFSPROC4_CLNT_SETACL,
};

/* NFSv3 procedure numbers — wire values from RFC 1813. Real values
 * in <linux/nfs3.h> (preempted). */
#define NFS3PROC_NULL         0
#define NFS3PROC_GETATTR      1
#define NFS3PROC_SETATTR      2
#define NFS3PROC_LOOKUP       3
#define NFS3PROC_ACCESS       4
#define NFS3PROC_READLINK     5
#define NFS3PROC_READ         6
#define NFS3PROC_WRITE        7
#define NFS3PROC_CREATE       8
#define NFS3PROC_MKDIR        9
#define NFS3PROC_SYMLINK      10
#define NFS3PROC_MKNOD        11
#define NFS3PROC_REMOVE       12
#define NFS3PROC_RMDIR        13
#define NFS3PROC_RENAME       14
#define NFS3PROC_LINK         15
#define NFS3PROC_READDIR      16
#define NFS3PROC_READDIRPLUS  17
#define NFS3PROC_FSSTAT       18
#define NFS3PROC_FSINFO       19
#define NFS3PROC_PATHCONF     20
#define NFS3PROC_COMMIT       21

/* enfs config knobs that the SUT switches on. */
#ifndef ENFS_MULTIPATH_ENABLE
#define ENFS_MULTIPATH_ENABLE 1
#endif

/* jiffies — controllable from tests via failover_time stubs. */
extern unsigned long jiffies;

/* Misc kernel constants used by failover_path.c's delay path. */
#ifndef HZ
#define HZ 1000UL
#endif
#ifndef MSEC_PER_SEC
#define MSEC_PER_SEC 1000UL
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#ifndef NFS3_OK
#define NFS3_OK 0
#endif

/* String / number parsers used by addr.c et al. Real kernel pulls
 * these in via <linux/kstrtox.h>; we declare here for SUTs that
 * don't include it directly. */
int kstrtou8(const char *s, unsigned int base, unsigned char *out);
unsigned int kstrtouint(const char *s, unsigned int base, unsigned int *out);
char *kstrdup(const char *s, unsigned int gfp);
size_t strlcat(char *dst, const char *src, size_t size);

/* IP-address parsers from <linux/inet.h>. */
int in4_pton(const char *src, int srclen, unsigned char *dst,
             int delim, const char **end);
int in6_pton(const char *src, int srclen, unsigned char *dst,
             int delim, const char **end);

/* Maximum lengths from <linux/inet.h>. */
#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN  16
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 48
#endif

/* IS_ENABLED(): kernel macro that resolves to 1 iff CONFIG_X is
 * defined (to either 1 or m). For tests we always want the IPv6
 * paths active. */
#ifndef CONFIG_IPV6
#define CONFIG_IPV6 1
#endif
#ifndef IS_ENABLED
#define __ARG_PLACEHOLDER_1 0,
#define ___is_defined(arg1_or_junk)  __take_second_arg(arg1_or_junk 1, 0)
#define __take_second_arg(__ignored, val, ...) val
#define __is_defined(x) ___is_defined(__ARG_PLACEHOLDER_##x)
#define IS_ENABLED(option) __is_defined(option)
#endif

/* IPv6 scope-id constants from <net/ipv6.h>. Tests don't drive
 * the scope-id branches of addr.c (those need real netdev infra),
 * but the SUT still needs the constants to compile. */
#ifndef IPV6_SCOPE_ID_LEN
#define IPV6_SCOPE_ID_LEN 16
#endif
#ifndef IPV6_SCOPE_DELIMITER
#define IPV6_SCOPE_DELIMITER '%'
#endif

/* Override snprintf so kernel-only %pI4 / %pI6 / %pI6c format
 * specifiers (used by sunrpc/addr.c et al.) work in userspace.
 * Implementation in tests/stubs/kernel_stubs.c. */
#include <stddef.h>
int enfs_test_snprintf(char *buf, size_t size, const char *fmt, ...);
#ifndef ENFS_KERNEL_STUBS_INTERNAL
#define snprintf enfs_test_snprintf
#endif

/* Misc kernel keywords + macros that some SUTs need but the shim
 * tree doesn't otherwise provide. */
#ifndef noinline
#define noinline __attribute__((noinline))
#endif
#ifndef __maybe_unused
#define __maybe_unused __attribute__((unused))
#endif
#ifndef __always_inline
#define __always_inline inline __attribute__((always_inline))
#endif
#ifndef struct_size
#define struct_size(p, member, n) (sizeof(*(p)) + sizeof((p)->member[0]) * (n))
#endif

/* Kernel min/max helpers used in xdr.c. */
#ifndef min_t
#define min_t(t, x, y) ({ t _x = (x); t _y = (y); _x < _y ? _x : _y; })
#endif
#ifndef max_t
#define max_t(t, x, y) ({ t _x = (x); t _y = (y); _x > _y ? _x : _y; })
#endif
#ifndef min
#define min(x, y) ({ typeof(x) _x = (x); typeof(y) _y = (y); _x < _y ? _x : _y; })
#endif
#ifndef max
#define max(x, y) ({ typeof(x) _x = (x); typeof(y) _y = (y); _x > _y ? _x : _y; })
#endif

/* xdr.c uses bvec_set_page; stub. */
struct bio_vec;
struct page;
static inline void bvec_set_page(struct bio_vec *bv, struct page *p,
                                  unsigned int len, unsigned int off)
{ (void)bv; (void)p; (void)len; (void)off; }

/* scatterlist forward decl + RPC auth max size; xdr.c uses these
 * in code paths the tests don't exercise. */
struct scatterlist {
    unsigned long page_link;
    unsigned int  offset;
    unsigned int  length;
};
#ifndef RPC_MAX_AUTH_SIZE
#define RPC_MAX_AUTH_SIZE 400
#endif

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
    atomic_t      path_state;     /* used by pm_state.c */
    struct sockaddr_storage srcaddr; /* used by pm_state.c diagnostics */
    int           protocol;       /* IPPROTO_TCP/UDP — diagnostics only */
    void         *stats;          /* opaque iostats pointer */
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
