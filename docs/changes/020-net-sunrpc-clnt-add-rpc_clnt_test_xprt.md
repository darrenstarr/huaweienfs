# 0020 — `net/sunrpc/clnt.c`: re-add `rpc_clnt_test_xprt` for enfs

Patch file: [`patches/ubuntu-7.0/0020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.patch`](../../patches/ubuntu-7.0/0020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.patch)

## What this change adds to stock Linux

A re-introduction of OE's older one-shot
`rpc_clnt_test_xprt(clnt, xprt, ops, data, flags)` API. Upstream
Linux moved to `rpc_clnt_setup_test_and_add_xprt()` (a more
elaborate variant) and dropped the simpler one. enfs's path-monitor
ping subsystem still calls the older API directly.

The new function is a byte-for-byte port of OE's implementation —
queue an async NULL RPC against the named xprt with the supplied
call_ops, return 1 on success, the error on failure.

```c
#if IS_ENABLED(CONFIG_SUNRPC_ENFS)
/* enfs needs the older one-shot test+add API. Reimplemented from
 * OpenEuler OLK-6.6 net/sunrpc/clnt.c. */
int rpc_clnt_test_xprt(struct rpc_clnt *clnt, struct rpc_xprt *xprt,
                       const struct rpc_call_ops *ops, void *data, int flags)
{
        struct rpc_task *task;

        task = rpc_call_null_helper(clnt, xprt, NULL,
                                    RPC_TASK_SOFT | RPC_TASK_SOFTCONN | flags,
                                    ops, data);
        if (IS_ERR(task))
                return PTR_ERR(task);
        rpc_put_task(task);
        return 1;
}
EXPORT_SYMBOL_GPL(rpc_clnt_test_xprt);
#endif
```

## Where it lives

- File: `net/sunrpc/clnt.c`
- Insertion point: ~line 3043, immediately before
  `rpc_clnt_setup_test_and_add_xprt()`.
- Wrapped in `#if IS_ENABLED(CONFIG_SUNRPC_ENFS)`.

## Why it's needed

enfs's per-path liveness monitor ("pm_ping") fires NULL-RPC probes
at each transport in a multipath set on a fixed schedule. The
probe-and-callback pattern matches the older OE API exactly:

```c
/* vendor/openeuler/fs/nfs/enfs/pm_ping.c:321 */
ret = rpc_clnt_test_xprt(work_info->clnt, work_info->xprt,
                         &pm_ping_set_status_ops, work_info, RPC_TASK_FIXED);

/* vendor/openeuler/fs/nfs/enfs/pm_ping.c:568 */
rpc_clnt_test_xprt(clnt, xprt, &pm_ping_set_status_ops, ..., RPC_TASK_FIXED);
```

Note the use of `RPC_TASK_FIXED` (the flag bit added by patch 0006)
in both call sites — pings must stay on their target xprt and
**not** be re-routed by failover, because the whole point of the
ping is to test "is *this specific transport* alive?".

The newer `rpc_clnt_setup_test_and_add_xprt()` doesn't do quite
what we need:

- It accepts an `rpc_xprt_switch` and adds the xprt to it.
- enfs has already added the xprt (during mount-time multipath
  construction); it just wants to test it.
- Attempting to re-add through the newer API would either no-op
  silently or error out, neither of which exercises the test path
  the way enfs needs.

A previous compat shim returned `0` unconditionally, which caused
enfs's `add_xprt` callback to dereference an uninitialised `rpc_task`
pointer (NULL deref crash on the first ping). The current
implementation actually queues a real RPC and returns the proper
status.

## Why this exact form

This is one of the patches that **deviates** from "literally what OE
ships":

- OE shipped `rpc_clnt_test_xprt` upstream-style (no `#ifdef` guard;
  it's just present in their tree). Stock upstream Linux dropped the
  function entirely. We re-add it but **wrap it in `#if
  IS_ENABLED(CONFIG_SUNRPC_ENFS)`** so a non-enfs build remains
  byte-for-byte the same as stock Ubuntu 7.0.
- The function body is OE's verbatim, copied from
  `vendor/openeuler/net/sunrpc/clnt.c:3030-3046`. The `RPC_TASK_SOFT
  | RPC_TASK_SOFTCONN` flag combination, the `rpc_call_null_helper`
  invocation, and the `return 1 / PTR_ERR(task)` semantics all
  match.
- The earlier-attempted shim that returned `0` is documented in the
  patch header. The shim was wrong because callers (notably
  `pm_ping.c`) expect "1 on success, error on failure" and treat
  any other return as "no task was queued, free the work_info now",
  which conflicts with the actual queued task that *does* run later
  with the same `work_info` pointer — leading to use-after-free or
  double-free, depending on timing.
- Returning `1` (not `0`) on success matches OE's convention for
  "task queued, callback will fire". Returning `0` would be
  conventional kernel "success", but enfs's callback design depends
  on the `1`.
- We do **not** also add `rpc_clnt_setup_test_and_add_xprt`-style
  semantics. enfs does not need that variant, and the compat shim
  is one symbol, not two.

## Observable effect for users

Without `enfs.ko` loaded: none. The function is built into
`sunrpc.ko` but has no in-tree caller.

With `enfs.ko` loaded and a multipath mount: the per-path liveness
monitor (`pm_ping.c`) starts firing NULL RPCs on its scheduled
interval. A user running `tcpdump` will see them as small periodic
NULL-procedure RPCs (NFSv3 procedure 0) to each transport in the
multipath set.

Failure modes:

- If a transport is unreachable, the NULL RPC times out (governed by
  `RPC_TASK_SOFT` / `RPC_TASK_SOFTCONN`), the callback marks that
  path as down, and enfs stops dispatching to it until the next
  successful ping.
- If the function returned `0` instead of `1` (the bug fixed by
  this patch), the callback path would crash on first ping with a
  NULL `rpc_task` deref.

No new `/proc` file is added by this patch alone; `enfs.ko` exposes
the per-path ping status via its own debugfs / proc nodes (added by
patches outside this series).
