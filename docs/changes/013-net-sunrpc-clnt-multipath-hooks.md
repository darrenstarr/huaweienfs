# 0013 — `net/sunrpc/clnt.c`: enfs multipath hook callsites

Patch file: [`patches/ubuntu-7.0/0013-net-sunrpc-clnt-multipath-hooks.patch`](../../patches/ubuntu-7.0/0013-net-sunrpc-clnt-multipath-hooks.patch)

## What this change adds to stock Linux

Eight hook sites in the SunRPC client core that thread enfs's
`rpc_multipath_ops_*` callbacks through the standard create / shutdown
/ dispatch / encode paths. All guarded by `CONFIG_SUNRPC_ENFS`.

The sites:

1. `#include <linux/sunrpc/sunrpc_enfs_adapter.h>`.
2. After `rpc_new_client` succeeds: `rpc_multipath_ops_create_clnt(args, clnt)`
   so enfs can attach per-client multipath state.
3. In `rpc_shutdown_client`, just before `rpc_release_client`:
   `rpc_multipath_ops_releas_clnt(clnt)` to drop that state. (Yes,
   the typo "releas" is preserved — see "Why this exact form".)
4. In the per-task xprt-acquisition path:
   `rpc_multipath_ops_inc_queuelen(xprt)` so enfs's per-xprt queue
   accounting stays in sync with the SunRPC `xps_queuelen` counter.
5. In `rpc_task_release_xprt`: matching
   `rpc_multipath_ops_dec_queuelen(xprt)`.
6. In `rpc_task_set_transport`:
   `rpc_multipath_ops_set_transport(task, clnt)` when there is an
   `rpc_proc` to dispatch.
7. In the RPC-call header encoder: when SUNRPC_ENFS is on, replace
   the fixed `cl_prog` / `cl_vers` writes with the
   `RPC_MULTIPAHT_UPDATE_RPC_PROC()` macro (defined in
   `sunrpc_enfs_adapter.h`) so enfs can rewrite the prog/vers per
   task — this is how the EXTEND probe rides over an existing
   multipath client.
8. In `rpc_clnt_xprt_switch_add_xprt`'s round-robin path: prefer
   `rpc_multipath_switch_set_roundrobin(clnt, xps)` (which falls
   through to the stock `rpc_xprt_switch_set_roundrobin(xps)` when
   no multipath is registered).

**Sample of the encoder rewrite** (the most subtle of the eight):

Before:

```c
        *p++ = req->rq_xid;
        *p++ = rpc_call;
        *p++ = cpu_to_be32(RPC_VERSION);
        *p++ = cpu_to_be32(clnt->cl_prog);
        *p++ = cpu_to_be32(clnt->cl_vers);
        *p   = cpu_to_be32(task->tk_msg.rpc_proc->p_proc);
```

After:

```c
        *p++ = req->rq_xid;
        *p++ = rpc_call;
        *p++ = cpu_to_be32(RPC_VERSION);
#if IS_ENABLED(CONFIG_SUNRPC_ENFS)
        RPC_MULTIPAHT_UPDATE_RPC_PROC(task, p, clnt);
#else
        *p++ = cpu_to_be32(clnt->cl_prog);
        *p++ = cpu_to_be32(clnt->cl_vers);
#endif
        *p   = cpu_to_be32(task->tk_msg.rpc_proc->p_proc);
```

The macro expansion (from `sunrpc_enfs_adapter.h`) writes prog/vers
from the task's own `rpc_proc` if set, otherwise falls back to
`clnt->cl_prog` / `clnt->cl_vers` — i.e. behaviour-preserving for
non-multipath clients.

## Where it lives

- File: `net/sunrpc/clnt.c`
- Include: ~line 41.
- `rpc_multipath_ops_create_clnt` in `rpc_new_client`: ~line 491.
- `rpc_multipath_ops_releas_clnt` in `rpc_shutdown_client`: ~line 970.
- `rpc_multipath_ops_inc_queuelen`: ~line 1101.
- `rpc_multipath_ops_dec_queuelen`: ~line 1118.
- `rpc_multipath_ops_set_transport` in `rpc_task_set_transport`:
  ~line 1180.
- `RPC_MULTIPAHT_UPDATE_RPC_PROC` in the call-header encoder: ~line
  2685.
- `rpc_multipath_switch_set_roundrobin`: ~line 3160.

## Why it's needed

These hooks are the SunRPC-side dispatch points for enfs. The
adapter implementations live in
`net/sunrpc/sunrpc_enfs_adapter.c` (dropped into the build tree by
`scripts/build-src-tree.sh`). When `enfs.ko` is not loaded, every
`rpc_multipath_ops_*` call resolves to a no-op stub
(`vendor/openeuler/include/linux/sunrpc/sunrpc_enfs_adapter.h:105+`).
When `enfs.ko` is loaded, it registers an `rpc_multipath_ops`
vtable and the stubs route into enfs's per-client / per-xprt logic.

Each hook serves a distinct purpose:

- **Create / release** — own the lifecycle of enfs's multipath
  state alongside the underlying `rpc_clnt`.
- **inc/dec queuelen** — keep enfs's per-xprt queue depth counter
  in sync with SunRPC's; enfs uses it to pick the least-loaded
  transport for round-robin.
- **set_transport** — for tasks that come from enfs (`RPC_TASK_ENFS`
  bit set, see patch 0006), let enfs override SunRPC's default
  xprt selection.
- **encoder rewrite** — lets enfs's EXTEND probe (which has a
  different prog/vers from the parent NFS client) reuse the parent
  client's `rpc_clnt` rather than constructing a new one per probe.
- **roundrobin** — lets enfs choose its own round-robin policy
  variant (e.g. weighted by latency) instead of stock SunRPC's
  uniform round-robin.

The adapter symbols this patch references all originate in
`vendor/openeuler/net/sunrpc/sunrpc_enfs_adapter.c`:

```c
/* :132 */ void rpc_multipath_ops_create_clnt(struct rpc_create_args *args, struct rpc_clnt *clnt)
/* :145 */ void rpc_multipath_ops_releas_clnt(struct rpc_clnt *clnt)
/* :156 */ void rpc_multipath_ops_inc_queuelen(struct rpc_xprt *xprt)
/* :167 */ void rpc_multipath_ops_dec_queuelen(struct rpc_xprt *xprt)
/* :194 */ void rpc_multipath_ops_set_transport(struct rpc_task *task, struct rpc_clnt *clnt)
```

## Why this exact form

- **Two deferred hooks.** OE has a `case -ETIMEDOUT` failover hook
  in `call_status` and another in `call_refresh`. Both depend on a
  local `failover` boolean we'd need to introduce; site-mapping is
  more invasive in 7.0 than in OE's 6.6 base. We deferred them and
  noted in the patch header. Net effect: failover-on-timeout is
  partially degraded — the load-balancing and initial-mount paths
  work, but a stuck transport will not be retired automatically by
  this set of hooks. A smoke test (mount + read) is unaffected.
- **Typo `releas_clnt` preserved.** OE's symbol name is
  `rpc_multipath_ops_releas_clnt` (sic — missing "e"). We keep the
  typo so the adapter `.c` files we vendor verbatim still link.
  Renaming would require touching three files.
- **Macro rewrite, not a function call, for the encoder.** The
  encoder is on the per-RPC hot path. Inlining via the macro means
  zero added function-call overhead when the multipath_option
  pointer is NULL (i.e. for non-multipath clients).
- **`rpc_multipath_switch_set_roundrobin` instead of editing the
  call site.** OE's helper itself falls through to the stock
  `rpc_xprt_switch_set_roundrobin` when no multipath is registered,
  so we just route through it. Single hook site, no behaviour
  change for non-enfs.

## Observable effect for users

For a stock build (`CONFIG_SUNRPC_ENFS=n`): zero — every hook is a
no-op via the `#if` guard.

For an enfs build with `enfs.ko` not loaded: zero — every adapter
function is a stub that early-returns.

For an enfs build with `enfs.ko` loaded but no multipath mount:
zero — adapter callbacks check `clnt->cl_enfs` (the bit added by
patch 0005) and early-return when it's clear.

For an enfs build with at least one multipath mount:

- enfs's round-robin policy drives transport selection on every RPC
  submission for that mount.
- The per-xprt queue depths visible via debugfs
  (`/sys/kernel/debug/sunrpc/...`) are the inputs enfs uses for
  load balancing.
- Capability probes (NFS3PROC_EXTEND) reuse the existing rpc_clnt
  via the encoder rewrite — no new rpc_clnt is created per probe.

The two deferred timeout-failover hooks (see "Why this exact form")
mean that, in this state, a permanently-down transport in a
multipath set is not automatically marked offline by this set of
hooks. Users running enfs in production should be aware of that
limitation until the deferred hooks land in a follow-up patch.
