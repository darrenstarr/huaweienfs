# 0017 — `net/sunrpc/xprtmultipath.c`: export 5 helpers for enfs

Patch file: [`patches/ubuntu-7.0/0017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.patch`](../../patches/ubuntu-7.0/0017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.patch)

## What this change adds to stock Linux

Consolidates five helper exports from `net/sunrpc/xprtmultipath.c`
that `enfs.ko` calls across the module boundary. Stock Ubuntu 7.0
keeps all five internal to `sunrpc.ko`:

| Helper | Stock state | Patch action |
|---|---|---|
| `xprt_switch_add_xprt_locked` | static | drop `static`, add `EXPORT_SYMBOL_GPL` |
| `rpc_xprt_switch_remove_xprt` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `xprt_switch_get` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `xprt_switch_put` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `xprt_iter_get_next` | non-static, no export | add `EXPORT_SYMBOL_GPL` |

No function body changes. Forward declarations of the now-exported
helpers live in `compat/enfs_compat.h` (dropped into the build tree
by `scripts/build-src-tree.sh`).

**Sample diff** (the one with both `static` removal and an export):

```c
-static void xprt_switch_add_xprt_locked(struct rpc_xprt_switch *xps,
+void xprt_switch_add_xprt_locked(struct rpc_xprt_switch *xps,
                struct rpc_xprt *xprt)
 {
        if (unlikely(xprt_get(xprt) == NULL))
@@
        xps->xps_nxprts++;
        xps->xps_nactive++;
 }
+EXPORT_SYMBOL_GPL(xprt_switch_add_xprt_locked);
```

For the other four helpers, only the `EXPORT_SYMBOL_GPL` line is
added.

## Where it lives

- File: `net/sunrpc/xprtmultipath.c`
- `xprt_switch_add_xprt_locked`: ~lines 32 / 44.
- `rpc_xprt_switch_remove_xprt`: ~line 95 (export only).
- `xprt_switch_get`: ~line 213 (export only).
- `xprt_switch_put`: ~line 226 (export only).
- `xprt_iter_get_next`: ~line 646 (export only).

## Why it's needed

These five helpers are the entire xprt-switch API surface enfs
needs. enfs's multipath logic (`enfs_multipath.c`,
`enfs_remount.c`, `shard_route.c`) drives the round-robin iterator
and the per-switch xprt list directly:

```c
/* vendor/openeuler/fs/nfs/enfs/enfs_multipath.c */
:393   xps = xprt_switch_get(rcu_dereference(clnt->cl_xpi.xpi_xpswitch));
:400   xprt_switch_put(xps);
:419   xprt_switch_add_xprt_locked(xps, xprt);

/* vendor/openeuler/fs/nfs/enfs/enfs_remount.c */
:93    rpc_xprt_switch_remove_xprt(xps, xprt, false);
:109   xps = xprt_switch_get(...);
:113   xprt_switch_put(xps);
:128   xprt_switch_put(xps);

/* vendor/openeuler/fs/nfs/enfs/shard_route.c */
:1560  xprt_switch_put(shard_work->xps);
:1585  xprt_switch_get(rcu_dereference(...));

/* xprt_iter_get_next is also called transitively via the failover
   path-state walks (every shard pick uses it). */
```

Without the exports, `enfs.ko` would fail to load with five distinct
`Unknown symbol` errors on `modprobe enfs`.

## Why this exact form

- **One patch for all five exports** rather than five separate
  one-line patches. OE upstream also handled them as a single
  cleanup; we keep that grouping. Easier to review as "the
  `xprtmultipath` export surface for enfs" than as five fragments.
- **`EXPORT_SYMBOL_GPL` placed immediately after the function body**,
  before the next function or kernel-doc block. Conventional kernel
  style.
- **No signature changes for any of the five.** Where OE shipped a
  changed signature (e.g. an extra arg for some internal use), we
  reverted to stock — we only need the original signatures and
  changing them would force enfs source edits which we want to
  avoid.
- **No deviation from OpenEuler.** OE applies the same five exports;
  this is a verbatim port.

## Observable effect for users

None directly. Pure symbol visibility change.

The downstream effect — once `enfs.ko` loads — is that enfs can
construct, query, and mutate xprt-switches directly, which is how it
implements:

- multipath construction at mount time (`xprt_switch_add_xprt_locked`),
- remount-time addition / removal of paths (same helper +
  `rpc_xprt_switch_remove_xprt`),
- per-RPC round-robin advancement (`xprt_iter_get_next`),
- refcount management around all of the above (`xprt_switch_get` /
  `_put`).

For a `/proc/kallsyms` consumer: all five helpers now appear with
`T` (text, exported) markers; `xprt_switch_add_xprt_locked` was
previously `t` (text, local).
