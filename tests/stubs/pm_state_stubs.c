// SPDX-License-Identifier: GPL-2.0
/*
 * pm_state_stubs.c — minimal stubs for test_pm_state.
 *
 * Differs from enfs_deps_stubs.c: when pm_state.c IS the source under
 * test, we must NOT also define pm_get_path_state in stubs (that would
 * give us two definitions of the same symbol). This file omits it,
 * keeps the rest, and adds stubs for the few external symbols pm_state
 * references that aren't otherwise stubbed.
 */
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/clnt.h>

/* enfs_xprt_context type, plus xprt_get_reserve_context /
 * xprt_set_reserve_context, are defined in enfs_preempt.h + the
 * shared enfs_deps_stubs.c. We do NOT include that here because it
 * defines pm_get_path_state — instead we re-implement the reserve-
 * context table inline below. */

#define STUB_RESERVE_CTX_MAX 512

struct stub_reserve_entry {
    struct rpc_xprt *xprt;
    void            *ctx;
};

static struct stub_reserve_entry stub_reserve_table[STUB_RESERVE_CTX_MAX];
static int                       stub_reserve_count;

void xprt_set_reserve_context(struct rpc_xprt *xprt, void *context)
{
    for (int i = 0; i < stub_reserve_count; i++) {
        if (stub_reserve_table[i].xprt == xprt) {
            stub_reserve_table[i].ctx = context;
            return;
        }
    }
    if (stub_reserve_count >= STUB_RESERVE_CTX_MAX) abort();
    stub_reserve_table[stub_reserve_count].xprt = xprt;
    stub_reserve_table[stub_reserve_count].ctx  = context;
    stub_reserve_count++;
}

void *xprt_get_reserve_context(struct rpc_xprt *xprt)
{
    for (int i = 0; i < stub_reserve_count; i++) {
        if (stub_reserve_table[i].xprt == xprt)
            return stub_reserve_table[i].ctx;
    }
    return NULL;
}

void stub_reserve_table_reset(void)
{
    memset(stub_reserve_table, 0, sizeof(stub_reserve_table));
    stub_reserve_count = 0;
}

/* Stubs for symbols pm_state.c references but doesn't define. */

/* in4_pton/in6_pton: kernel address-parsers used by is_valid_ip_address
 * to determine whether the user-supplied "local" string is a valid IP
 * literal. Stub returns 1 (success) for non-empty input, 0 for empty.
 * Real validation happens elsewhere — pm_state's branch is just for
 * choosing whether to call rpc_localalladdr or use ctx->srcaddr. */
int in4_pton(const char *src, int srclen, unsigned char *dst,
             int delim, const char **end)
{
    (void)dst; (void)delim; (void)end;
    if (!src || srclen == 0 || (srclen < 0 && *src == 0))
        return 0;
    return 1;
}
int in6_pton(const char *src, int srclen, unsigned char *dst,
             int delim, const char **end)
{
    (void)dst; (void)delim; (void)end;
    if (!src || srclen == 0 || (srclen < 0 && *src == 0))
        return 0;
    return 1;
}

/* rpc_localalladdr: enumerate local addresses for the auto-bind path.
 * Stubbed to return 0 (no addresses). pm_state.c only uses the result
 * for the diagnostic string, so behaviour is observable only in the
 * formatted output we don't assert on. */
struct sockaddr;
size_t rpc_localalladdr(struct rpc_xprt *xprt, struct sockaddr *buf,
                        size_t buflen)
{
    (void)xprt; (void)buf; (void)buflen;
    return 0;
}

/* Reset hook called from test setup. */
void stub_reset_all(void)
{
    stub_reserve_table_reset();
}

/* Re-export stub_set_path_state — but for test_pm_state we use the
 * REAL pm_set_path_state on the SUT side. The stub here is only
 * needed so test setup compiles. Provide a no-op (using int instead
 * of enum to avoid the redeclaration that would happen when the SUT
 * also pulls in enfs_preempt.h's enum definition). */
void stub_set_path_state(struct rpc_xprt *xprt, int s)
{
    (void)xprt; (void)s;
}
