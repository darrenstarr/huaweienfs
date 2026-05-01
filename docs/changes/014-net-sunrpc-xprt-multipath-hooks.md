# 0014 — `net/sunrpc/xprt.c`: enfs multipath hooks (timeout, iostat, servername)

Patch file: [`patches/ubuntu-7.0/0014-net-sunrpc-xprt-multipath-hooks.patch`](../../patches/ubuntu-7.0/0014-net-sunrpc-xprt-multipath-hooks.patch)

## What this change adds to stock Linux

Seven hook sites in the SunRPC xprt layer that let enfs participate
in transport-level scheduling, accounting, and naming. All guarded by
`CONFIG_SUNRPC_ENFS`.

The sites:

1. `#include <linux/sunrpc/sunrpc_enfs_adapter.h>`.
2. Two `out_sleep:` paths in `xprt_reserve_xprt_cong()` /
   `xprt_reserve_xprt()`: call
   `rpc_multipath_ops_adjust_task_timeout(task, NULL)` before the
   sleep so enfs can recompute the per-path timeout based on which
   transport was selected.
3. `xprt_wait_for_buffer_space()`: same `adjust_task_timeout` call
   on `xprt->snd_task` before flagging `XPRT_WRITE_SPACE`.
4. `xprt_alloc_slot()` (immediately after `xprt_init_majortimeo`):
   call `rpc_multipath_ops_init_task_req(task, req)` so enfs can
   stamp the rqst with its multipath-specific bookkeeping.
5. `xprt_release()`: call `rpc_multipath_ops_xprt_iostat(task)` so
   enfs's per-xprt I/O accounting sees every release.
6. `xprt_create_transport()`: replace the unconditional
   `kstrdup(args->servername, GFP_KERNEL)` with
   `rpc_multipath_set_servername(args->servername, GFP_KERNEL)`
   (which lets enfs attach a per-name multipath context); also call
   `rpc_multipath_ops_create_xprt(xprt)` and bail out cleanly if the
   enfs side fails to allocate.
7. `xprt_destroy()`: replace `kfree(xprt->servername)` with
   `rpc_multipath_free_servername(xprt)` — it knows whether the name
   carries enfs metadata that needs cleanup.

**Sample of the `xprt_create_transport` rewrite**:

Before:

```c
        xprt->servername = kstrdup(args->servername, GFP_KERNEL);
        if (xprt->servername == NULL) {
                xprt_destroy(xprt);
                return ERR_PTR(-ENOMEM);
        }

        rpc_xprt_debugfs_register(xprt);
```

After:

```c
#if IS_ENABLED(CONFIG_SUNRPC_ENFS)
        xprt->servername = rpc_multipath_set_servername(args->servername, GFP_KERNEL);
#else
        xprt->servername = kstrdup(args->servername, GFP_KERNEL);
#endif
        if (xprt->servername == NULL) {
                xprt_destroy(xprt);
                return ERR_PTR(-ENOMEM);
        }

#if IS_ENABLED(CONFIG_SUNRPC_ENFS)
        if (!rpc_multipath_ops_create_xprt(xprt)) {
                xprt_destroy(xprt);
                return ERR_PTR(-ENOMEM);
        }
#endif

        rpc_xprt_debugfs_register(xprt);
```

## Where it lives

- File: `net/sunrpc/xprt.c`
- Include: ~line 54.
- `out_sleep` adjust_task_timeout (cong variant): ~line 287.
- `out_sleep` adjust_task_timeout (non-cong variant): ~line 356.
- `xprt_wait_for_buffer_space`: ~line 619.
- `xprt_alloc_slot` init_task_req: ~line 1915.
- `xprt_release` xprt_iostat: ~line 1991.
- `xprt_create_transport` set_servername + create_xprt: ~line
  2105 / 2113.
- `xprt_destroy` free_servername: ~line 2143.

## Why it's needed

Each hook addresses a distinct enfs runtime requirement:

- **`adjust_task_timeout` (3 sites)** — when an RPC is about to
  sleep waiting for an xprt to become available, enfs needs to
  recompute the major-timeout against the *new* transport's RTT
  estimate, not the original transport's. Without this, slow
  paths inherit fast paths' tight timeouts and time out
  prematurely.
- **`init_task_req`** — enfs stamps each `rpc_rqst` with
  multipath-specific metadata (which transport was selected, the
  selection epoch, etc.) right after the standard SunRPC
  initialisation. This is the only safe slot in the
  `xprt_alloc_slot` flow before the rqst is visible to other CPUs.
- **`xprt_iostat`** — the per-xprt iostat counters (bytes, RPCs,
  latency-bucket counts) feed enfs's load-balancing decisions.
  Hooking on every release captures the full lifecycle.
- **`set_servername` / `free_servername`** — for multipath xprts,
  the servername string carries an embedded prefix (`"<idx>:<name>"`)
  that enfs uses to tell siblings apart. The custom alloc/free
  pair owns that embedded format; using stock `kstrdup`/`kfree`
  would corrupt it.
- **`create_xprt`** — allocates the per-xprt enfs state (path-state
  cache, ping accounting). Returns false on ENOMEM and we fail the
  whole xprt creation rather than silently leaving the xprt
  half-initialised.

The adapter implementations:

```c
/* vendor/openeuler/net/sunrpc/sunrpc_enfs_adapter.c */
:70   const char *rpc_multipath_set_servername(const char *s, gfp_t gfp)
:88   void rpc_multipath_free_servername(struct rpc_xprt *xprt)
:178  bool rpc_multipath_ops_create_xprt(struct rpc_xprt *xprt)
:222  void rpc_multipath_ops_xprt_iostat(struct rpc_task *task)
:254  void rpc_multipath_ops_adjust_task_timeout(struct rpc_task *task, ...)
:265  void rpc_multipath_ops_init_task_req(struct rpc_task *task, ...)
```

## Why this exact form

- **Replace, don't wrap, the servername alloc / free.** OE chose
  full replacement over a "call stock then mutate" pattern because
  the multipath-aware form needs to reserve extra leading bytes for
  its prefix; allocating twice would be wasteful and racy. We keep
  the same shape.
- **`adjust_task_timeout` fires *before* the actual `rpc_sleep_on_*`
  call.** Critical: the timeout is a parameter to the sleep, so the
  recompute has to happen first. Both `out_sleep` sites preserve
  this order.
- **`create_xprt` returns `bool`, not `int`.** OE's signature; we
  match. The single failure mode (ENOMEM) gets translated to
  `-ENOMEM` at the single call site.
- **Hook order matches OE byte-for-byte.** Same rationale as patch
  0010 — minimises rebase work next time we re-sync.
- **No deviation from OE here.** Unlike the clnt.c hooks (patch
  0013) which deferred two failover sites, all seven xprt hooks
  ported cleanly.

## Observable effect for users

For a stock build (`CONFIG_SUNRPC_ENFS=n`): zero — every hook is
behind an `#if` and the `kstrdup` / `kfree` paths are unchanged.

For an enfs build without `enfs.ko` loaded: zero — all adapter
functions resolve to no-op stubs (the
`rpc_multipath_set_servername` stub falls through to `kstrdup`,
`rpc_multipath_free_servername` to `kfree`, etc.).

For an enfs build with `enfs.ko` loaded and a multipath mount:

- Per-path timeouts adapt to per-path RTT, not the original
  transport's RTT. Mixed-latency multipath sets (e.g. one local-DC
  path + one cross-DC path) behave more sensibly.
- Per-xprt iostat counters (visible via SunRPC debugfs) reflect
  enfs's accounting.
- Each transport in a multipath set has a synthetic servername like
  `"0:server"`, `"1:server"`, ... visible in
  `/sys/kernel/debug/sunrpc/rpc_xprt/...`. This is the only
  external-facing surface change from this patch and it is
  diagnostic-only.
