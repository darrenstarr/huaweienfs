// SPDX-License-Identifier: GPL-2.0
/*
 * enfs_path_stubs.c — minimal stubs for test_enfs_path.
 *
 * enfs_path.c calls rpc_free_iostats and uses the reserve-context
 * slot. Reuse the reserve-context table from the standard stubs by
 * including the same code; provide a no-op rpc_free_iostats.
 */
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/sunrpc/xprt.h>

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
    for (int i = 0; i < stub_reserve_count; i++)
        if (stub_reserve_table[i].xprt == xprt)
            return stub_reserve_table[i].ctx;
    return NULL;
}

/* rpc_free_iostats: production frees the per-clnt iostats block. The
 * stub frees whatever pointer it gets so callers can hand it malloc()'d
 * memory in tests. */
void rpc_free_iostats(void *stats) { free(stats); }

void stub_reset_all(void)
{
    /* Note: we don't free the ctx pointers because the SUT is what
     * allocates them with kzalloc (which our shim maps to calloc).
     * Each test runs in its own fork, so leaks are bounded. */
    memset(stub_reserve_table, 0, sizeof(stub_reserve_table));
    stub_reserve_count = 0;
}
