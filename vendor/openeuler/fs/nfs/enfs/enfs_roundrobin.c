// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2025-2025. All rights reserved.
 */
#include <linux/spinlock.h>
#include <linux/module.h>
#include <linux/printk.h>
#include <linux/kref.h>
#include <linux/rculist.h>
#include <linux/types.h>
#include <linux/sunrpc/xprt.h>
#include <linux/sunrpc/clnt.h>
#include <linux/sunrpc/xprtmultipath.h>
#include "enfs_roundrobin.h"

#include "enfs.h"
#include "enfs_config.h"
#include "pm_state.h"
#include "enfs_proc.h"

typedef struct rpc_xprt *(*enfs_xprt_switch_find_xprt_t)(
	struct rpc_xprt_switch *xps, const struct rpc_xprt *cur);
static const struct rpc_xprt_iter_ops enfs_xprt_iter_roundrobin;
static const struct rpc_xprt_iter_ops enfs_xprt_iter_singular;

static bool enfs_xprt_is_active(struct rpc_xprt *xprt)
{
	enum enfs_path_state state;

	if (kref_read(&xprt->kref) <= 0)
		return false;

	state = pm_get_path_state(xprt);
	if (enfs_is_path_connected(state))
		return true;

	return false;
}

static struct rpc_xprt *
enfs_lb_set_cursor_xprt(struct rpc_xprt_switch *xps, struct rpc_xprt **cursor,
			enfs_xprt_switch_find_xprt_t find_next)
{
	struct rpc_xprt *pos;
	struct rpc_xprt *old;

	old = smp_load_acquire(cursor); // multi thread access
	pos = find_next(xps, old);
	smp_store_release(cursor, pos); // multi thread access
	return pos;
}

/*
 * Pure round-robin: walk the xprt list from `cur+1`, skipping inactive
 * transports and (when the native link is down) the main one. Returns
 * the first eligible xprt; wraps to the head of the list if `cur` was
 * the last one.
 *
 * Replaced the original "least-queued masquerading as round-robin"
 * algorithm in favour of true round-robin for two measured reasons:
 *
 *   1. Sync I/O dispatch (psync, direct I/O — the project's main
 *      lab/customer workload) only ever has 1 RPC in flight per task,
 *      so atomic_long_read(&ctx->queuelen) was uniformly 0 across all
 *      xprts at decision time. The "least-queued" tie-break never had
 *      anything to break ties on; the algorithm degenerated to "first
 *      active xprt" with extra atomic reads.
 *
 *   2. At the high-IOPS regime where queuelen variance becomes real
 *      (DPC-equivalent, > 100 K IOPS aggregate), the per-dispatch
 *      atomic reads across ~16 transports become a measurable
 *      cacheline-bouncing cost that pure round-robin avoids entirely.
 *
 * See docs/internals/12-perf-tuning.md §12.4.1 for the analysis +
 * benchmark numbers.
 */
static struct rpc_xprt *
enfs_lb_find_next_entry_roundrobin(struct rpc_xprt_switch *xps,
				   const struct rpc_xprt *cur)
{
	struct rpc_xprt *pos;
	struct rpc_xprt *first_eligible = NULL;
	bool past_cur = (cur == NULL);
	int nativeLinkStatus = enfs_get_native_link_io_status();

	list_for_each_entry_rcu(pos, &xps->xps_xprt_list, xprt_switch) {
		bool eligible =
			(nativeLinkStatus || !enfs_is_main_xprt(pos)) &&
			enfs_xprt_is_active(pos);

		if (eligible) {
			/* Track the first eligible xprt so we can wrap the
			 * cursor to the head of the list when `cur` is the
			 * last one. */
			if (first_eligible == NULL)
				first_eligible = pos;
			if (past_cur)
				return pos;
		}
		/* Mark cursor passage even for ineligible (inactive / main-
		 * skipped) xprts so a cursor pointing at one still advances
		 * to the next eligible xprt rather than wrapping to head. */
		if (pos == cur)
			past_cur = true;
	}
	/* Cursor was past the end (or `cur` is no longer in the list);
	 * wrap to the first eligible xprt. NULL if list is empty / all
	 * xprts inactive — caller falls back to the main xprt. */
	return first_eligible;
}

struct rpc_xprt *
enfs_lb_switch_find_first_active_xprt(struct rpc_xprt_switch *xps)
{
	struct rpc_xprt *pos;

	list_for_each_entry_rcu(pos, &xps->xps_xprt_list, xprt_switch) {
		if (enfs_xprt_is_active(pos))
			return pos;
	};
	return NULL;
}

struct rpc_xprt *enfs_lb_switch_get_main_xprt(struct rpc_xprt_switch *xps)
{
	return list_first_or_null_rcu(&xps->xps_xprt_list, struct rpc_xprt,
				      xprt_switch);
}

static struct rpc_xprt *
enfs_lb_switch_get_next_xprt_roundrobin(struct rpc_xprt_switch *xps,
					const struct rpc_xprt *cur)
{
	struct rpc_xprt *xprt;

	// disable multipath
	if (enfs_get_config_multipath_state())
		return enfs_lb_switch_get_main_xprt(xps);

	xprt = enfs_lb_find_next_entry_roundrobin(xps, cur);
	if (xprt != NULL)
		return xprt;
	return enfs_lb_switch_get_main_xprt(xps);
}

static struct rpc_xprt *
enfs_lb_iter_next_entry_roundrobin(struct rpc_xprt_iter *xpi)
{
	struct rpc_xprt_switch *xps = rcu_dereference(xpi->xpi_xpswitch);

	if (xps == NULL)
		return NULL;

	return enfs_lb_set_cursor_xprt(xps, &xpi->xpi_cursor,
				       enfs_lb_switch_get_next_xprt_roundrobin);
}

static struct rpc_xprt *
enfs_lb_switch_find_singular_entry(struct rpc_xprt_switch *xps,
				   const struct rpc_xprt *cur)
{
	struct rpc_xprt *pos;
	bool found = false;

	list_for_each_entry_rcu(pos, &xps->xps_xprt_list, xprt_switch) {
		if (cur == pos)
			found = true;
		if (found && enfs_xprt_is_active(pos))
			return pos;
	}
	return NULL;
}

struct rpc_xprt *enfs_lb_get_singular_xprt(struct rpc_xprt_switch *xps,
					   const struct rpc_xprt *cur)
{
	struct rpc_xprt *xprt;

	if (xps == NULL)
		return NULL;
	// disable multipath
	if (enfs_get_config_multipath_state())
		return enfs_lb_switch_get_main_xprt(xps);

	if (cur == NULL || xps->xps_nxprts < 2) {
		xprt = enfs_lb_switch_find_first_active_xprt(xps);
		if (!xprt)
			goto main_xprt;
		return xprt;
	}

	xprt = enfs_lb_switch_find_singular_entry(xps, cur);
	if (!xprt) {
		xprt = enfs_lb_switch_find_first_active_xprt(xps);
		if (!xprt)
			goto main_xprt;
	}
	return xprt;

main_xprt:
	return enfs_lb_switch_get_main_xprt(xps);
}

static struct rpc_xprt *
enfs_lb_iter_next_entry_sigular(struct rpc_xprt_iter *xpi)
{
	struct rpc_xprt_switch *xps = rcu_dereference(xpi->xpi_xpswitch);

	if (xps == NULL)
		return NULL;

	return enfs_lb_set_cursor_xprt(xps, &xpi->xpi_cursor,
				       enfs_lb_get_singular_xprt);
}

static void enfs_lb_iter_default_rewind(struct rpc_xprt_iter *xpi)
{
	WRITE_ONCE(xpi->xpi_cursor, NULL);
}

static void enfs_lb_switch_set_roundrobin(struct rpc_clnt *clnt)
{
	struct rpc_xprt_switch *xps;

	rcu_read_lock();
	xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
	rcu_read_unlock();

	if (xps == NULL || xps->xps_nxprts == 0)
		return;

	if (clnt->cl_vers == 3) {
		if (READ_ONCE(xps->xps_iter_ops) !=
		    &enfs_xprt_iter_roundrobin) {
			WRITE_ONCE(xps->xps_iter_ops,
				   &enfs_xprt_iter_roundrobin);
		}
		return;
	}
	if (READ_ONCE(xps->xps_iter_ops) != &enfs_xprt_iter_singular)
		WRITE_ONCE(xps->xps_iter_ops, &enfs_xprt_iter_singular);
}

static struct rpc_xprt *enfs_lb_switch_find_current(struct list_head *head,
						    const struct rpc_xprt *cur)
{
	struct rpc_xprt *pos;

	list_for_each_entry_rcu(pos, head, xprt_switch) {
		if (cur == pos)
			return pos;
	}
	return NULL;
}

static struct rpc_xprt *enfs_lb_iter_current_entry(struct rpc_xprt_iter *xpi)
{
	struct rpc_xprt_switch *xps = rcu_dereference(xpi->xpi_xpswitch);
	struct list_head *head;

	if (xps == NULL)
		return NULL;
	head = &xps->xps_xprt_list;
	if (xpi->xpi_cursor == NULL || xps->xps_nxprts < 2)
		return enfs_lb_switch_get_main_xprt(xps);
	return enfs_lb_switch_find_current(head, xpi->xpi_cursor);
}

int enfs_lb_set_policy(struct rpc_clnt *clnt, void *data)
{
	if (clnt->cl_enfs == 1)
		enfs_lb_switch_set_roundrobin(clnt);

	return 0;
}

static const struct rpc_xprt_iter_ops enfs_xprt_iter_roundrobin = {
	.xpi_rewind = enfs_lb_iter_default_rewind,
	.xpi_xprt = enfs_lb_iter_current_entry,
	.xpi_next = enfs_lb_iter_next_entry_roundrobin,
};

static const struct rpc_xprt_iter_ops enfs_xprt_iter_singular = {
	.xpi_rewind = enfs_lb_iter_default_rewind,
	.xpi_xprt = enfs_lb_iter_current_entry,
	.xpi_next = enfs_lb_iter_next_entry_sigular,
};

const struct rpc_xprt_iter_ops *enfs_xprt_rr_ops(void)
{
	return &enfs_xprt_iter_roundrobin;
}

const struct rpc_xprt_iter_ops *enfs_xprt_singular_ops(void)
{
	return &enfs_xprt_iter_singular;
}

bool enfs_is_rr_route(struct rpc_clnt *clnt)
{
	bool ret = false;
	struct rpc_xprt_switch *xps;

	rcu_read_lock();
	xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
	if (!xps || !xps->xps_iter_ops) {
		rcu_read_unlock();
		return ret;
	}
	if (xps->xps_iter_ops == &enfs_xprt_iter_roundrobin)
		ret = true;
	rcu_read_unlock();

	return ret;
}

bool enfs_is_singularr_route(struct rpc_clnt *clnt)
{
	bool ret = false;
	struct rpc_xprt_switch *xps;

	rcu_read_lock();
	xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
	if (!xps || !xps->xps_iter_ops) {
		rcu_read_unlock();
		return ret;
	}
	if (xps->xps_iter_ops == &enfs_xprt_iter_singular)
		ret = true;
	rcu_read_unlock();

	return ret;
}

int enfs_lb_revert_policy(struct rpc_clnt *clnt, void *data)
{
	struct rpc_xprt_switch *xps;

	if (clnt->cl_enfs == 1) {
		rcu_read_lock();
		xps = rcu_dereference(clnt->cl_xpi.xpi_xpswitch);
		rcu_read_unlock();
		rpc_xprt_switch_set_singular(xps);
	}

	return 0;
}

int enfs_lb_init(void)
{
	enfs_iter_rpc_clnt(enfs_lb_set_policy, NULL);

	return 0;
}

void enfs_lb_exit(void)
{
	enfs_iter_rpc_clnt(enfs_lb_revert_policy, NULL);
}
