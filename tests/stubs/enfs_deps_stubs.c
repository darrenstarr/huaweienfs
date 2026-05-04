// SPDX-License-Identifier: GPL-2.0
/*
 * enfs_deps_stubs.c — fakes for enfs-internal functions that the
 * source under test calls into.
 *
 * Each fake is paired with global control variables (`stub_*`) that
 * tests set in the test setup phase to drive behavior. The fakes
 * are deliberately the simplest thing that compiles and gives tests
 * a way to control the source-under-test.
 *
 * Tests should reset these globals in setup(), so test order doesn't
 * matter and isolation is per-fork (Check default).
 */

#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtmultipath.h>

/* Pulled in via the force-included enfs_preempt.h, which declares:
 *   enum enfs_path_state, struct enfs_xprt_context,
 *   xprt_get_reserve_context, xprt_set_reserve_context,
 *   enfs_get_config_multipath_state, enfs_get_native_link_io_status,
 *   pm_get_path_state, enfs_iter_rpc_clnt, enfs_is_main_xprt,
 *   rpc_xprt_switch_set_singular, rpc_xprt_switch_set_roundrobin
 */

/* ------------- Per-xprt reserve-context table ----------------------
 * The kernel side stores enfs_xprt_context inside struct rpc_xprt
 * via xprt->ctx (a private slot). In tests, we use a small flat
 * lookup table keyed by xprt pointer. Simpler than extending the
 * shim's struct rpc_xprt with an extra field, and lets tests
 * construct contexts independently of xprts.
 * ------------------------------------------------------------------ */

#define STUB_RESERVE_CTX_MAX 64

struct stub_reserve_entry {
    struct rpc_xprt *xprt;
    void            *ctx;
};

static struct stub_reserve_entry stub_reserve_table[STUB_RESERVE_CTX_MAX];
static int                       stub_reserve_count;

void xprt_set_reserve_context(struct rpc_xprt *xprt, void *context)
{
    /* Replace if present, else append. */
    for (int i = 0; i < stub_reserve_count; i++) {
        if (stub_reserve_table[i].xprt == xprt) {
            stub_reserve_table[i].ctx = context;
            return;
        }
    }
    if (stub_reserve_count >= STUB_RESERVE_CTX_MAX) {
        /* Test harness exceeded its budget. Loud failure. */
        abort();
    }
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

/* ------------- enfs_is_main_xprt --------------------------------- */
/* Production version is `static inline` in enfs.h — preempted, so
 * we provide it here. Reads the .main field of the reserve ctx. */
bool enfs_is_main_xprt(struct rpc_xprt *xprt)
{
    struct enfs_xprt_context *ctx = xprt_get_reserve_context(xprt);
    return ctx && ctx->main;
}

/* ------------- pm_get_path_state ---------------------------------
 * Each xprt in tests has a controllable path_state. Stored in a
 * parallel table for the same reason as reserve-context. */

#define STUB_PATH_STATE_MAX 64

struct stub_path_state_entry {
    struct rpc_xprt      *xprt;
    enum enfs_path_state  state;
};

static struct stub_path_state_entry stub_path_state_table[STUB_PATH_STATE_MAX];
static int                          stub_path_state_count;

void stub_set_path_state(struct rpc_xprt *xprt, enum enfs_path_state s)
{
    for (int i = 0; i < stub_path_state_count; i++) {
        if (stub_path_state_table[i].xprt == xprt) {
            stub_path_state_table[i].state = s;
            return;
        }
    }
    if (stub_path_state_count >= STUB_PATH_STATE_MAX) abort();
    stub_path_state_table[stub_path_state_count].xprt  = xprt;
    stub_path_state_table[stub_path_state_count].state = s;
    stub_path_state_count++;
}

enum enfs_path_state pm_get_path_state(struct rpc_xprt *xprt)
{
    for (int i = 0; i < stub_path_state_count; i++) {
        if (stub_path_state_table[i].xprt == xprt)
            return stub_path_state_table[i].state;
    }
    return PM_STATE_UNDEFINED;
}

void stub_path_state_table_reset(void)
{
    memset(stub_path_state_table, 0, sizeof(stub_path_state_table));
    stub_path_state_count = 0;
}

/* ------------- enfs_get_config_* --------------------------------- */

int32_t stub_multipath_state           = 0; /* ENFS_MULTIPATH_ENABLE = 0 */
int32_t stub_native_link_io_status     = 1; /* default: native link IO ok */

int32_t enfs_get_config_multipath_state(void)
{
    return stub_multipath_state;
}

int32_t enfs_get_native_link_io_status(void)
{
    return stub_native_link_io_status;
}

/* ------------- enfs_iter_rpc_clnt -------------------------------
 * Production walks a registered clnt list. Tests don't currently
 * exercise enfs_lb_init / enfs_lb_exit; the stub records that it
 * was called so we can assert on it if a test wants to. */

int  stub_iter_rpc_clnt_calls;

void enfs_iter_rpc_clnt(int (*fn)(struct rpc_clnt *clnt, void *data),
                        void *data)
{
    (void)fn; (void)data;
    stub_iter_rpc_clnt_calls++;
}

/* ------------- rpc_xprt_switch_set_{roundrobin,singular} --------
 * Called from enfs_lb_revert_policy. Tests can read these counters
 * to confirm the right code path ran. */

int stub_set_singular_calls;
int stub_set_roundrobin_calls;

void rpc_xprt_switch_set_singular(struct rpc_xprt_switch *xps)
{
    (void)xps;
    stub_set_singular_calls++;
}

void rpc_xprt_switch_set_roundrobin(struct rpc_xprt_switch *xps)
{
    (void)xps;
    stub_set_roundrobin_calls++;
}

/* ------------- Convenience: full reset for test setup ------------ */

void stub_reset_all(void)
{
    stub_reserve_table_reset();
    stub_path_state_table_reset();
    stub_multipath_state        = 0;
    stub_native_link_io_status  = 1;
    stub_iter_rpc_clnt_calls    = 0;
    stub_set_singular_calls     = 0;
    stub_set_roundrobin_calls   = 0;
}
