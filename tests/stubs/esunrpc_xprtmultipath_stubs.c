// SPDX-License-Identifier: GPL-2.0
/*
 * esunrpc_xprtmultipath_stubs.c — kernel-API stubs for
 * vendor/esunrpc/net/esunrpc/xprtmultipath.c.
 *
 * The SUT pulls in many kernel primitives (slab, RCU, IDR, sysfs).
 * This file provides the userspace equivalents.
 */
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <netinet/in.h>

/* IDR: kernel-style integer-to-pointer ID allocator. The SUT uses
 * it to assign each switch a unique xps_id. Stub returns a
 * monotonically increasing counter. */
struct idr { int next; };
static struct idr xprt_multipath_ids = { .next = 0 };
static pthread_mutex_t xprt_multipath_ids_lock = PTHREAD_MUTEX_INITIALIZER;

void idr_init(struct idr *idr) { idr->next = 0; }
int  idr_alloc(struct idr *idr, void *ptr, int start, int end, unsigned int gfp)
{
    (void)ptr; (void)start; (void)end; (void)gfp;
    pthread_mutex_lock(&xprt_multipath_ids_lock);
    int id = idr->next++;
    pthread_mutex_unlock(&xprt_multipath_ids_lock);
    return id;
}
void idr_remove(struct idr *idr, int id) { (void)idr; (void)id; }
void idr_destroy(struct idr *idr) { idr->next = 0; }

/* xprt_get/xprt_put: refcount via the embedded kref. */
struct rpc_xprt;
struct kref;
static inline int xkref_read(struct kref *k) { return ((int *)k)[0]; }

/* Sysfs stubs: SUT registers + unregisters per-switch sysfs objects.
 * Tests don't observe sysfs, so all no-ops. */
struct rpc_sysfs_xprt_switch;
struct rpc_sysfs_xprt_switch *
rpc_sysfs_xprt_switch_alloc(struct rpc_xprt_switch *xps, unsigned int gfp)
{ (void)xps; (void)gfp; return (void *)1; /* non-NULL sentinel */ }
void rpc_sysfs_xprt_switch_setup(struct rpc_sysfs_xprt_switch *s,
                                 struct rpc_xprt *x, unsigned int gfp)
{ (void)s; (void)x; (void)gfp; }
void rpc_sysfs_xprt_switch_destroy(struct rpc_xprt_switch *xps)
{ (void)xps; }
void rpc_sysfs_xprt_setup(struct rpc_xprt_switch *xps,
                          struct rpc_xprt *xprt,
                          unsigned int gfp)
{ (void)xps; (void)xprt; (void)gfp; }

/* Net namespace pointer: tests pass NULL; SUT only stashes it. */
struct net;
struct net *get_net(struct net *n) { return n; }
void put_net(struct net *n) { (void)n; }

/* xprt_get/xprt_put: ref-bump on the rpc_xprt's kref. */
struct rpc_xprt *xprt_get(struct rpc_xprt *x) { return x; }
void xprt_put(struct rpc_xprt *x) { (void)x; }

/* RCU: tests are single-threaded; synchronize_rcu is a no-op. */
void synchronize_rcu_expedited(void) { }
void synchronize_rcu(void) { }
void call_rcu(void *head, void (*func)(void *))
{
    /* Eager invocation in tests. */
    if (func) func(head);
}

/* esunrpc_rpc_cmp_addr / esunrpc_rpc_cmp_addr_port — referenced by
 * xprtmultipath.c via the rpc_addr.h chain. Stub on memcmp. */
int esunrpc_rpc_cmp_addr(const struct sockaddr *a, const struct sockaddr *b)
{
    if (a->sa_family != b->sa_family) return 0;
    if (a->sa_family == AF_INET) {
        return memcmp(&((struct sockaddr_in *)a)->sin_addr,
                      &((struct sockaddr_in *)b)->sin_addr,
                      sizeof(struct in_addr)) == 0;
    }
    if (a->sa_family == AF_INET6) {
        return memcmp(&((struct sockaddr_in6 *)a)->sin6_addr,
                      &((struct sockaddr_in6 *)b)->sin6_addr,
                      sizeof(struct in6_addr)) == 0;
    }
    return 0;
}

/* Reset hook. */
void stub_reset_all(void)
{
    pthread_mutex_lock(&xprt_multipath_ids_lock);
    xprt_multipath_ids.next = 0;
    pthread_mutex_unlock(&xprt_multipath_ids_lock);
}
