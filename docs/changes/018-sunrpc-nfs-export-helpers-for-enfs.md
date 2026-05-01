# 0018 — sunrpc/nfs: export `xprt_release` and `nfs3_procedures` for enfs

Patch file: [`patches/ubuntu-7.0/0018-sunrpc-nfs-export-helpers-for-enfs.patch`](../../patches/ubuntu-7.0/0018-sunrpc-nfs-export-helpers-for-enfs.patch)

## What this change adds to stock Linux

Two `EXPORT_SYMBOL_GPL` lines, in two different files. Both targets
are non-static in stock Ubuntu 7.0 but un-exported; `enfs.ko` calls
both across the module boundary.

| Symbol | File | Stock state | Patch action |
|---|---|---|---|
| `xprt_release` | `net/sunrpc/xprt.c` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `nfs3_procedures` | `fs/nfs/nfs3xdr.c` | non-static, no export | add `EXPORT_SYMBOL_GPL` |

**Diff for `xprt_release`**:

```c
        else
                xprt_free_bc_request(req);
 }
+EXPORT_SYMBOL_GPL(xprt_release);

 #ifdef CONFIG_SUNRPC_BACKCHANNEL
```

**Diff for `nfs3_procedures`**:

```c
        PROC(PATHCONF,          getattr,        pathconf,       0),
        PROC(COMMIT,            commit,         commit,         5),
 };
+EXPORT_SYMBOL_GPL(nfs3_procedures);

 static unsigned int nfs_version3_counts[ARRAY_SIZE(nfs3_procedures)];
```

## Where it lives

- `xprt_release` export: `net/sunrpc/xprt.c`, ~line 1996,
  immediately after the function body.
- `nfs3_procedures` export: `fs/nfs/nfs3xdr.c`, ~line 2475,
  immediately after the array's closing brace.

Note: the `nfs3_procedures` export here is on the *stock* version of
the array — the array still has only the standard 22 NFSv3 entries
at this point in the patch series. Patch 0012 separately adds the
`PROC(EXTEND, ...)` slot **before** this export line, but the two
patches are independent — patch 0018 would still apply (and the
export would still be useful) if patch 0012 were dropped.

## Why it's needed

### `xprt_release`

enfs's failover path needs to release the current xprt before the
task is reassigned to a new one (otherwise the per-task xprt
reference leaks):

```c
/* vendor/openeuler/fs/nfs/enfs/failover_path.c:34 */
xprt_release(task);

/* vendor/openeuler/fs/nfs/enfs/failover_path.c:278 */
xprt_release(task);

/* vendor/openeuler/fs/nfs/enfs/shard_route.c:957 */
xprt_release(task);
```

This is the standard SunRPC "drop the xprt this task was holding"
helper; enfs needs it to safely re-route a task without leaking xprt
refcounts.

### `nfs3_procedures`

enfs's extended-call subsystem dispatches the EXTEND op (see patch
0012) directly against the procedure table:

```c
/* vendor/openeuler/fs/nfs/enfs/exten_call.c:609 */
.rpc_proc = &nfs3_procedures[NFS3PROC_EXTEND],

/* vendor/openeuler/fs/nfs/enfs/exten_call.c:646 */
.rpc_proc = &nfs3_procedures[NFS3PROC_EXTEND],
```

The `rpc_proc` field of an outgoing `rpc_message` is a pointer into
this array. enfs takes the pointer, doesn't allocate its own copy.
Without the export, `enfs.ko` would fail to link against the symbol.

## Why this exact form

- **Two unrelated symbols in one patch.** OE bundles them too, on
  the rationale that they are both "exports purely for enfs" — the
  shared motivation makes them reviewable together. Splitting would
  produce two trivial one-line patches.
- **`EXPORT_SYMBOL_GPL`, not `EXPORT_SYMBOL`.** Consistent with the
  rest of this series' export grants and with enfs's GPL license.
- **Placement immediately after the function body / array definition.**
  Standard kernel style; no intervening blank lines or comments.
- **Did not touch `nfs3_procedures`'s `const` qualifier.** Some
  exports of read-only arrays go through a wrapper that strips
  const; this one doesn't need to. enfs reads the array, does not
  mutate it.
- **No deviation from OE.** Both exports are present in OE's
  upstream kernel; we are re-applying the same two-line change
  against stock Ubuntu 7.0.

## Observable effect for users

None directly. Pure symbol visibility — function bodies and array
contents are unchanged.

The downstream effect — once `enfs.ko` is loaded — is:

- enfs's failover machinery can release stuck xprt references
  cleanly (otherwise we'd leak one xprt ref per failover event).
- enfs's EXTEND-op dispatch can target `nfs3_procedures[22]`
  directly. Without the export this dispatch would need to
  re-implement the procedure-info struct in enfs's own source, with
  obvious drift risk.

For a `/proc/kallsyms` consumer: both symbols now have `T` markers
instead of `t`.
