# 0016 — `net/sunrpc/clnt.c`: export `rpc_task_get_next_xprt` for enfs

Patch file: [`patches/ubuntu-7.0/0016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.patch`](../../patches/ubuntu-7.0/0016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.patch)

## What this change adds to stock Linux

Drops `static` from `rpc_task_get_next_xprt()` and adds an
`EXPORT_SYMBOL_GPL` so that out-of-tree callers — specifically
`enfs.ko`'s failover path — can use it across the module boundary.

The function itself is unchanged. Stock Ubuntu 7.0 already has it; it
is just file-private.

**Before** (`net/sunrpc/clnt.c`, ~line 1153):

```c
static struct rpc_xprt *
rpc_task_get_next_xprt(struct rpc_clnt *clnt)
{
        return rpc_task_get_xprt(clnt, xprt_iter_get_next(&clnt->cl_xpi));
}
```

**After**:

```c
struct rpc_xprt *
rpc_task_get_next_xprt(struct rpc_clnt *clnt)
{
        return rpc_task_get_xprt(clnt, xprt_iter_get_next(&clnt->cl_xpi));
}
EXPORT_SYMBOL_GPL(rpc_task_get_next_xprt);
```

## Where it lives

- File: `net/sunrpc/clnt.c`
- `static` removal: ~line 1156.
- `EXPORT_SYMBOL_GPL`: ~line 1160 (immediately after the function
  body).

## Why it's needed

`enfs.ko`'s failover path needs to "advance" a stuck task to the next
transport in the round-robin iterator without re-implementing the
iterator walk (which would risk drifting from the SunRPC-internal
walk over time):

```c
/* vendor/openeuler/fs/nfs/enfs/failover_path.c:37 */
task->tk_xprt = rpc_task_get_next_xprt(task->tk_client);

/* vendor/openeuler/fs/nfs/enfs/failover_path.c:244 */
task->tk_xprt = rpc_task_get_next_xprt(task->tk_client);

/* vendor/openeuler/fs/nfs/enfs/failover_path.c:281 */
task->tk_xprt = rpc_task_get_next_xprt(clnt);
```

Three call sites, all in `failover_path.c`, all reaching for the
same helper. Without the export, `enfs.ko` would fail to load with
`Unknown symbol rpc_task_get_next_xprt`.

The function itself is small — it's a thin wrapper that combines
`xprt_iter_get_next` (returns the next `xprt` from the round-robin
iterator) with `rpc_task_get_xprt` (bumps the refcount and returns
it). enfs uses it from its failover state machine to move a task
that errored on the current xprt onto the next one in the rotation.

## Why this exact form

- **`EXPORT_SYMBOL_GPL`, not `EXPORT_SYMBOL`.** This whole port
  uses `_GPL` consistently — enfs is GPL-licensed, and this matches
  every other helper SunRPC exports.
- **`static` removed cleanly, no compat shim.** Some vendored
  helpers in this series go through a wrapper that adds telemetry
  or arg-translation; this one does not need any. The function
  signature is already what enfs wants.
- **No deviation from OpenEuler.** OE shipped this same export
  upstream; we are just re-applying the same one-line change against
  stock Ubuntu 7.0 (which dropped the export when it last re-synced
  from mainline).

## Observable effect for users

None directly. This is a pure symbol-visibility change. The
function body, behaviour, refcount semantics, and all callers in
`net/sunrpc/clnt.c` itself are unchanged.

The downstream effect — once `enfs.ko` is loaded — is that the
failover path can advance a stuck task to the next transport. Without
this export, `enfs.ko` would not even load, so the entire enfs
feature becomes unavailable.

For a debugger / `kallsyms` consumer: `rpc_task_get_next_xprt` now
appears in `/proc/kallsyms` with a `T` (text, exported) marker
instead of a `t` (text, local) marker.
