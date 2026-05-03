# Chapter 2 — Multipath at the SunRPC layer

## 2.1 Where this chapter starts and ends

[Chapter 1](./01-architecture.md) established that `enfs.ko`
registers a `struct rpc_multipath_ops` into `sunrpc.ko` at module-load
time, and that `sunrpc.ko` calls into that vector at carefully chosen
hook sites in `clnt.c` and `xprt.c`. This chapter zooms in on the
pieces of `sunrpc.ko` that get touched: the `rpc_xprt_switch` data
structure, the `rpc_xprt_iter` cursor that walks it, the round-robin
policy that enfs installs in place of the stock one, and the
`cl_enfs` bit on `struct rpc_clnt` that gates all of this.

By the end of the chapter the reader should be able to trace one
RPC — say, an `nfs_read` — from the moment `rpc_run_task` is called
to the moment its `task->tk_xprt` is bound to a specific transport,
and explain which lines of which files in this checkout decided which
transport got picked.

The sibling concerns — *how* a transport gets created and added to
the switch in the first place, and *how* the path-monitor changes a
transport's state from active to faulty — are the subject of
[chapter 3](./03-nfs-mount-flow.md) and chapters
[4](./04-pm-ping-state.md)/[5](./05-failover.md) respectively. This
chapter assumes the switch is already populated and the states are
already meaningful.

## 2.2 The `rpc_xprt_switch` and the `rpc_xprt_iter`

### 2.2.1 What stock SunRPC provides

SunRPC has had multipath plumbing in mainline since 2016 (Trond
Myklebust's rework for pNFS / `nconnect=`). The core data structure
is in
[`vendor/ubuntu-7.0/include/linux/sunrpc/xprtmultipath.h`](../../vendor/ubuntu-7.0/include/linux/sunrpc/xprtmultipath.h)
(materialised after `bash scripts/fetch-vendor-ubuntu.sh ubuntu-7.0`):

```c
struct rpc_xprt_switch {
        spinlock_t                      xps_lock;
        struct kref                     xps_kref;
        unsigned int                    xps_id;
        unsigned int                    xps_nxprts;
        unsigned int                    xps_nactive;
        unsigned int                    xps_nunique_destaddr_xprts;
        atomic_long_t                   xps_queuelen;
        struct list_head                xps_xprt_list;
        struct net                     *xps_net;
        const struct rpc_xprt_iter_ops *xps_iter_ops;
        struct rpc_sysfs_xprt_switch   *xps_sysfs;
        struct rcu_head                 xps_rcu;
};

struct rpc_xprt_iter {
        struct rpc_xprt_switch __rcu   *xpi_xpswitch;
        struct rpc_xprt                *xpi_cursor;
        const struct rpc_xprt_iter_ops *xpi_ops;
};

struct rpc_xprt_iter_ops {
        void (*xpi_rewind)(struct rpc_xprt_iter *);
        struct rpc_xprt *(*xpi_xprt) (struct rpc_xprt_iter *);
        struct rpc_xprt *(*xpi_next) (struct rpc_xprt_iter *);
};
```

Three things are worth noting. First, the switch owns a list of
`rpc_xprt`s linked through their `xprt_switch` list_head; the list
is RCU-walked under `rcu_read_lock`, modified under `xps_lock`, and
the switch itself is reference-counted by `xps_kref`. Second, the
iterator (`xpi_*`) is a thin cursor into the switch with its own ops
vector — the switch can publish a default policy via `xps_iter_ops`,
but a particular cursor can override it via its private `xpi_ops`.
Third, every `struct rpc_clnt` carries one `cl_xpi` field of type
`rpc_xprt_iter`; that is the cursor SunRPC consults when it needs to
pick a transport for the next RPC on this client.

### 2.2.2 What enfs adds to the picture

enfs does not extend `rpc_xprt_switch` itself — patch 0005 only
touches `rpc_clnt`, not `rpc_xprt_switch`. (The
[`docs/PORTING-NOTES.md`](../PORTING-NOTES.md) drift table records
"`struct rpc_xprt_switch` unchanged" between OLK-6.6 and Ubuntu 7.0,
and the build doesn't need a shim for it.) What enfs *does* do is:

- Attach a per-transport context blob behind `xprt->servername`,
  retrieved with `xprt_get_reserve_context()`. The blob is allocated
  by `enfs_alloc_xprt_ctx` (`fs/nfs/enfs/enfs_path.c:15`) and
  contains `struct enfs_xprt_context` — see
  [`vendor/openeuler/fs/nfs/enfs/enfs.h:63`](../../vendor/openeuler/fs/nfs/enfs/enfs.h):

  ```c
  struct enfs_xprt_context {
          int                  version;
          struct sockaddr_storage srcaddr;
          struct rpc_iostats *stats;
          bool                 main;
          atomic_t             path_state;
          atomic_t             path_check_state;
          atomic_long_t        queuelen;
          ...
  };
  ```

- Replace the switch's default `xps_iter_ops` (set by stock
  `rpc_xprt_switch_set_roundrobin`) with one of two enfs-defined
  vectors: `enfs_xprt_iter_roundrobin` for NFSv3 multipath, or
  `enfs_xprt_iter_singular` for NFSv4 (where pNFS layout choices
  prefer "stay on the same xprt unless forced off"). The choice is
  made in `enfs_lb_switch_set_roundrobin`
  ([`enfs_roundrobin.c:216`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c)).

The transport-context blob is the trick that lets enfs keep per-xprt
metadata (path state, per-xprt queue length, source address) without
modifying `struct rpc_xprt`'s on-disk layout. Patch 0017 exports the
helpers (`xprt_switch_get`, `xprt_switch_put`, `xprt_iter_get_next`,
`xprt_switch_add_xprt_locked`, `rpc_xprt_switch_remove_xprt`) so
`enfs.ko` can manipulate these structures across the module boundary.

Forward-reference: how the context blob is *backed* (it is appended
to the kmalloc that holds `xprt->servername`, see
`rpc_multipath_set_servername` /
`rpc_multipath_free_servername` in
[`vendor/openeuler/net/sunrpc/sunrpc_enfs_adapter.c:70`](../../vendor/openeuler/net/sunrpc/sunrpc_enfs_adapter.c))
is detailed in [chapter 7](./07-locking-refcounts.md).

## 2.3 The `cl_enfs` bit

Stock `struct rpc_clnt` has, near its top, a packed bitfield with
`cl_softrtry`, `cl_intr`, `cl_autobind`, `cl_chatty`,
`cl_shutdown` and so on. Patch 0005 inserts a single new bit,
`cl_enfs`, into this field:

```c
#if defined(__GENKSYMS__) || !IS_ENABLED(CONFIG_SUNRPC_ENFS)
                cl_shutdown : 1,/* rpc immediate -EIO */
#else
                cl_shutdown : 1,/* rpc immediate -EIO */
                cl_enfs     : 1,/* enfs multipath enabled */
#endif
                cl_netunreach_fatal : 1;
```

(See [`patches/ubuntu-7.0/0005-include-sunrpc-clnt.h-add-multipath-fields.patch`](../../patches/ubuntu-7.0/0005-include-sunrpc-clnt.h-add-multipath-fields.patch)
for the full hunk.) The genksyms guard makes the CRC of every
exported symbol that mentions `struct rpc_clnt` continue to match
stock — see [chapter 9](./09-genksyms-crc.md). The bitfield form
(rather than a separate `unsigned long flags` field) was chosen for
the same reason: a `flags` word would shift every subsequent struct
field, breaking the CRCs of half a dozen exports. A single bit
slipped into an existing 16-bit packed bitfield costs zero bytes
and zero offset shifts.

The bit is set in exactly one place across the entire codebase:

[`vendor/openeuler/fs/nfs/enfs/enfs_multipath.c:871`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)

```c
create_args->clnt->cl_enfs = 1;
enfs_xprt_ippair_create(&xprtargs, create_args->clnt, mount_options);
```

This is inside `enfs_multipath_create_thread`, which is only invoked
when an enfs mount has produced a non-NULL
`args->multipath_option`. The full chain that gets there is:

1. User mounts with `remoteaddrs=`. The fs_context parser stashes a
   `struct multipath_mount_options *` on `ctx->enfs_option` (see
   [chapter 3](./03-nfs-mount-flow.md)).
2. `nfs.ko/client.c:nfs_init_server` copies that pointer into
   `cl_init->enfs_option` (patch 0019).
3. `nfs.ko/client.c:nfs_create_rpc_client` copies it again into
   `rpc_create_args.multipath_option` (patch 0019).
4. `sunrpc.ko/clnt.c` constructs the `rpc_clnt`, then runs
   `rpc_multipath_ops_create_clnt(args, clnt)` (patch 0013).
5. The adapter forwards to `enfs.ko`'s `enfs_create_multi_xprt`
   ([`enfs_multipath.c:919`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)),
   which kicks off `enfs_multipath_create_thread`, which sets the
   bit and adds the extra transports.

Because `cl_enfs` defaults to zero on every freshly-constructed
`rpc_clnt`, every code path that doesn't pass through enfs's
`create_clnt` callback continues to behave exactly like stock
SunRPC. That is the cleanest version of the *opt-in* story: a
non-enfs mount produces an `rpc_clnt` with `cl_enfs == 0` and every
enfs hook treats it as a no-op.

The bit is consumed in three places:

- [`enfs_roundrobin.c:266`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c):
  `enfs_lb_set_policy` only installs the round-robin iterator ops if
  `clnt->cl_enfs == 1`.
- [`enfs_roundrobin.c:333`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c):
  `enfs_lb_revert_policy` (called on module exit) only reverts
  if `cl_enfs == 1`.
- [`sunrpc_enfs_adapter.c:217`](../../vendor/openeuler/net/sunrpc/sunrpc_enfs_adapter.c):
  the `rpc_clnt_has_multipath(clnt)` predicate, used by the
  `rpc_multipath_switch_set_roundrobin` inline (declared in
  [`include/linux/sunrpc/sunrpc_enfs_adapter.h:70`](../../vendor/openeuler/include/linux/sunrpc/sunrpc_enfs_adapter.h))
  to decide whether to call enfs's iterator-installer or fall back
  to the stock `rpc_xprt_switch_set_roundrobin`.

## 2.4 The `multipath_option` void pointer

Patch 0005 also adds a `void *multipath_option` to `struct rpc_clnt`
and to `struct rpc_create_args`, both wrapped in a
`__GENKSYMS__` guard and placed at the *end* of the struct so stock
callers reading older fields by offset are unaffected.

Lifetime, allocation, and ownership:

- **Allocator**: `enfs.ko`'s `nfs_multipath_alloc_options`
  ([`enfs_multipath_parse.c`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath_parse.c) —
  called from `nfs_multipath_parse_options` at line 626), invoked
  via the adapter from `nfs.ko/fs_context.c` when the user passes
  `remoteaddrs=` or `localaddrs=` to mount.
- **Held by**: first by `struct nfs_fs_context::enfs_option` (added
  by patch 0009); then copied (pointer-copy, not deep copy) into
  `struct nfs_client_initdata::enfs_option` and then into
  `struct rpc_create_args::multipath_option` by patch 0019. After
  `rpc_create` returns, the pointer is consumed by enfs's
  `create_clnt` callback (`enfs_create_multi_xprt`), which uses the
  parsed IP lists to attach extra transports and then frees the
  options inside `enfs_multipath_create_thread`
  ([`enfs_multipath.c:874`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)
  releases the wrapping `cargs`/`thargs`; the mount-options blob
  itself is released by `enfs_free_mount_options` from the
  `nfs_fs_context_free` path, patch 0011).
- **Freer**: `enfs.ko`'s `nfs_multipath_free_options`
  (`enfs_multipath_parse.c:653`), invoked through the adapter by
  `nfs_fs_context_free` and by `nfs_reconfigure` on remount-failure
  ([patch 0010](../../patches/ubuntu-7.0/0010-fs-nfs-super-add-enfs-hooks.patch)).

The pointer is an opaque `void *` from `sunrpc.ko`'s perspective —
SunRPC never dereferences it, only forwards it back into the
adapter. This is what lets the field pass cleanly through the
module boundary: `sunrpc.ko` doesn't know the type, and even if
`enfs.ko` is unloaded mid-flight, no SunRPC code path follows the
pointer.

## 2.5 Picking a transport for an RPC

This is the central runtime question of the chapter. Walk through
what happens when `nfs_read` calls `rpc_run_task` on an enfs-mounted
file. The relevant lines in
[`vendor/ubuntu-7.0/net/sunrpc/clnt.c`](../../vendor/ubuntu-7.0/net/sunrpc/clnt.c):

```c
1224:  struct rpc_task *rpc_run_task(const struct rpc_task_setup *task_setup_data)
1225:  {
1226:          struct rpc_task *task;
1227:          task = rpc_new_task(task_setup_data);
...
1235:          rpc_task_set_client(task, task_setup_data->rpc_client);
1236:          rpc_task_set_rpc_message(task, task_setup_data->rpc_message);
1237:
1238:          if (task->tk_action == NULL)
1239:                  rpc_call_start(task);
1240:
1241:          atomic_inc(&task->tk_count);
1242:          rpc_execute(task);
1243:          return task;
1244:  }
```

The transport-binding step happens inside `rpc_task_set_client`
(line 1179), which calls `rpc_task_set_transport` (line 1163):

```c
1162:  static
1163:  void rpc_task_set_transport(struct rpc_task *task, struct rpc_clnt *clnt)
1164:  {
1165:          if (task->tk_xprt) { ... return or release ... }
1172:          if (task->tk_flags & RPC_TASK_NO_ROUND_ROBIN)
1173:                  task->tk_xprt = rpc_task_get_first_xprt(clnt);
1174:          else
1175:                  task->tk_xprt = rpc_task_get_next_xprt(clnt);
1176:  }
```

Patch 0013 inserts an enfs hook at the top of this function:

```c
#if IS_ENABLED(CONFIG_SUNRPC_ENFS)
        if (task->tk_msg.rpc_proc)
                rpc_multipath_ops_set_transport(task, clnt);
#endif
```

The hook lets enfs pre-bind `task->tk_xprt` to a deliberately-chosen
transport before stock SunRPC would otherwise pick one. enfs's
implementation of this op (`enfs_set_transport`,
[`enfs_multipath.c:987`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c))
defers to `shard_set_transport` only when *shard routing* is in
effect; for plain round-robin enfs lets `rpc_task_get_next_xprt`
do the work. (Shard routing is an optional enfs feature where
specific file ranges are pinned to specific transports based on a
server-side hint; covered in [chapter 6](./06-extend-op.md).)

The next step is `rpc_task_get_next_xprt`, defined at
[`clnt.c:1156`](../../vendor/ubuntu-7.0/net/sunrpc/clnt.c). Stock
keeps it `static`; patch 0016 changes that to:

```c
struct rpc_xprt *
rpc_task_get_next_xprt(struct rpc_clnt *clnt)
{
        return rpc_task_get_xprt(clnt, xprt_iter_get_next(&clnt->cl_xpi));
}
EXPORT_SYMBOL_GPL(rpc_task_get_next_xprt);
```

The `static` had to go because enfs's failover code calls this
across the module boundary
([`failover_path.c:244`](../../vendor/openeuler/fs/nfs/enfs/failover_path.c)
inside `reselect_xprt`); without `EXPORT_SYMBOL_GPL` the linker
would not resolve the reference at module-load time.

Inside `rpc_task_get_next_xprt`, the actual policy lives in
`xprt_iter_get_next`. From the stock implementation in
`net/sunrpc/xprtmultipath.c` (extracted from
`/home/darren/.cache/enfs-vendor/linux_7.0.0-14.14/linux-7.0.0/net/sunrpc/xprtmultipath.c`):

```c
struct rpc_xprt *xprt_iter_get_next(struct rpc_xprt_iter *xpi)
{
        struct rpc_xprt *xprt;
        rcu_read_lock();
        xprt = xprt_iter_get_helper(xpi, xprt_iter_ops(xpi)->xpi_next);
        rcu_read_unlock();
        return xprt;
}
```

The dispatch goes through `xprt_iter_ops(xpi)`, which prefers the
iterator's private `xpi_ops` if set, otherwise consults the switch's
`xps_iter_ops`. enfs only ever installs a switch-level policy (via
`enfs_lb_switch_set_roundrobin` writing to `xps_iter_ops`); it does
not push per-iterator overrides. So the chain ends at one of
enfs's two iterator vectors:

[`vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c:272`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c)

```c
static const struct rpc_xprt_iter_ops enfs_xprt_iter_roundrobin = {
        .xpi_rewind = enfs_lb_iter_default_rewind,
        .xpi_xprt   = enfs_lb_iter_current_entry,
        .xpi_next   = enfs_lb_iter_next_entry_roundrobin,
};
```

`enfs_lb_iter_next_entry_roundrobin` (line 142) calls
`enfs_lb_set_cursor_xprt` (line 41) with
`enfs_lb_switch_get_next_xprt_roundrobin` as the picker (line 126),
which in turn calls `enfs_lb_find_next_entry_roundrobin` (line 54).

That last function is the heart of the policy. Annotated:

```c
static struct rpc_xprt *
enfs_lb_find_next_entry_roundrobin(struct rpc_xprt_switch *xps,
                                   const struct rpc_xprt *cur)
{
        struct rpc_xprt *pos, *prev = NULL;
        struct rpc_xprt *min_queuelen_xprt = NULL;
        struct rpc_xprt *optimal_xprt = NULL;
        unsigned long min_xprt_queuelen = 0, optimal_queuelen = 0;
        bool found = false;
        int nativeLinkStatus = enfs_get_native_link_io_status();

        list_for_each_entry_rcu(pos, &xps->xps_xprt_list, xprt_switch) {
                if (!nativeLinkStatus && enfs_is_main_xprt(pos))
                        continue;                /* skip the "native" xprt
                                                  * (the one created by
                                                  * the original NFS mount,
                                                  * before enfs added the
                                                  * extras), if disabled */

                if (!enfs_xprt_is_active(pos)) {
                        prev = pos;
                        continue;                /* skip xprts whose
                                                  * pm_state isn't NORMAL
                                                  * or UNSTABLE */
                }

                ctx = xprt_get_reserve_context(pos);
                pos_xprt_queuelen = atomic_long_read(&ctx->queuelen);

                /* track the global min queuelen across the whole switch */
                if (min_queuelen_xprt == NULL ||
                    pos_xprt_queuelen < min_xprt_queuelen) {
                        min_queuelen_xprt = pos;
                        min_xprt_queuelen = pos_xprt_queuelen;
                }

                if (cur == prev)
                        found = true;

                /* among the xprts AFTER the cursor, prefer the one with
                 * the lowest queue length, but bail out early if we find
                 * one with zero queue */
                if (found && (optimal_xprt == NULL ||
                              optimal_queuelen < min_xprt_queuelen)) {
                        if (min_xprt_queuelen == 0)
                                return pos;
                        optimal_xprt = pos;
                        optimal_queuelen = pos_xprt_queuelen;
                }
                prev = pos;
        }
        return optimal_xprt ? optimal_xprt : min_queuelen_xprt;
}
```

The policy is *not* pure round-robin in the strict "step exactly one
position per RPC" sense. It is "step from the cursor and prefer the
least-loaded xprt among the rest, falling back to the
globally-least-loaded xprt if the loop wraps". In practice on an
N-server mount with even load this still distributes RPCs fairly
across the N transports — verified empirically in the README's
"1 MiB NFS reads round-robin across 4 servers" claim
([`README.md:71`](../../README.md)).

The per-xprt queue length used here is `ctx->queuelen`, which is
incremented from the SunRPC adapter's
`rpc_multipath_ops_inc_queuelen` hook (called from `rpc_task_get_xprt`
in `clnt.c:1099`, via patch 0013) and decremented from
`rpc_multipath_ops_dec_queuelen` (called from `rpc_task_release_xprt`,
`clnt.c:1116`). enfs's implementations are
`enfs_inc_queuelen` and `enfs_dec_queuelen` at
[`enfs_multipath.c:996`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c).

## 2.6 Path state and the `enfs_xprt_is_active` predicate

`enfs_xprt_is_active` is the gate that lets the round-robin iterator
ignore broken transports. It lives at
[`enfs_roundrobin.c:26`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c):

```c
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
```

`pm_get_path_state` reads `ctx->path_state` out of the per-xprt
context blob; `enfs_is_path_connected` (inline in
[`pm_state.h:23`](../../vendor/openeuler/fs/nfs/enfs/pm_state.h))
returns true for `PM_STATE_NORMAL` and `PM_STATE_UNSTABLE`, false
for everything else.

The state machine itself has four values:

```c
enum enfs_path_state {
        PM_STATE_INIT,
        PM_STATE_NORMAL,
        PM_STATE_UNSTABLE,
        PM_STATE_FAULT,
        PM_STATE_UNDEFINED      /* xprt is not multipath xprt */
};
```

(`PM_STATE_UNDEFINED` is returned by `pm_get_path_state` when the
context lookup fails, e.g. for an `rpc_xprt` that was never enrolled
in enfs. It is treated as not-active by every enfs callsite.)

Transitions, by code site:

- **INIT** is the initial state set by
  [`enfs_multipath.c:299`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)
  inside `enfs_add_xprt_setup` for every freshly-created multipath
  xprt, just before the first ping is queued.
- **NORMAL** is set in two places:
  [`enfs_multipath.c:893`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)
  when the *main* xprt (the one carried over from the original NFS
  mount) gets its multipath context allocated, and
  [`pm_ping.c:187`](../../vendor/openeuler/fs/nfs/enfs/pm_ping.c)
  when a ping succeeds and the reconnect-time history says the path
  has been stable.
- **UNSTABLE** is set in
  [`pm_ping.c:189`](../../vendor/openeuler/fs/nfs/enfs/pm_ping.c)
  when a ping succeeds but the path has reconnected too often (the
  reconnect-time ring buffer is full). The path remains usable —
  the iterator still treats UNSTABLE as active — but enfs will
  prefer a NORMAL path if one is available; the comparison is
  outside the iterator, in shard routing
  ([`shard_route.c:753`](../../vendor/openeuler/fs/nfs/enfs/shard_route.c)).
- **FAULT** is set by either
  [`failover_path.c:192`](../../vendor/openeuler/fs/nfs/enfs/failover_path.c)
  (when an in-flight RPC on this xprt times out, via the
  `failover_handle` op) or
  [`pm_ping.c:285`](../../vendor/openeuler/fs/nfs/enfs/pm_ping.c)
  (when a ping fails after exhausting retries).

The `pm_set_path_state` setter
([`pm_state.c:74`](../../vendor/openeuler/fs/nfs/enfs/pm_state.c))
takes the xprt with `xprt_get`, atomically writes the new state into
the context's `path_state` field, logs a state-transition message
(`"path state change from {%d} to {%d}"`), and `xprt_put`s. It is
the only writer for `ctx->path_state`; readers use
`atomic_read(&ctx->path_state)` directly.

The full ping-driven side of the state machine — the workqueue, the
NULL-RPC dispatch, the reconnect-time ring buffer — is the subject
of [chapter 4](./04-pm-ping-state.md). The failover side is
[chapter 5](./05-failover.md). For this chapter the important point
is just that the iterator's `enfs_xprt_is_active` check is what
makes a FAULT path invisible to the round-robin walk; the next RPC
on this client will skip it.

## 2.7 The exported helpers from `xprtmultipath.c`

Patch 0017 promotes five functions from `xprtmultipath.c` from
"static or non-static-but-unexported" to "non-static and
EXPORT_SYMBOL_GPL". Each one is touched because `enfs.ko` calls it
across the module boundary:

| Symbol                          | Was       | Why enfs needs it |
|---------------------------------|-----------|-------------------|
| `xprt_switch_add_xprt_locked`   | `static`  | `enfs_xprt_switch_add_xprt` ([`enfs_multipath.c:419`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)) calls it under `xps_lock` to add a freshly-pinged xprt to the switch. The stock `rpc_xprt_switch_add_xprt` wrapper takes the lock itself, but enfs already holds it from `enfs_xprt_switch_add_xprt`. |
| `rpc_xprt_switch_remove_xprt`   | unexported| Used by live remount when `enfs_remount.c` removes an xprt whose IP-pair the user has dropped from `remoteaddrs=`. |
| `xprt_switch_get`               | unexported| `enfs_already_have_xprt` ([`enfs_multipath.c:392`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c)) uses it to walk `xps_xprt_list` while holding an xps reference, so the switch can't be freed under the iteration. |
| `xprt_switch_put`               | unexported| Symmetric drop of the above. Also used by `failover_path.c`. |
| `xprt_iter_get_next`            | unexported| Called both internally (out of `rpc_task_get_next_xprt`, which is itself patched to `EXPORT_SYMBOL_GPL` by patch 0016) and externally by enfs failover code. |

Forward declarations of the now-exported functions live in
`compat/enfs_compat.h` so every translation unit in `enfs.ko` sees a
prototype without dragging in `<linux/sunrpc/xprtmultipath.h>` just
for forward-decl purposes.

A separate patch (0018) does the same trick for two more symbols:
`xprt_release` (called from enfs's failover path to finalise an RPC
after re-selection) and `nfs3_procedures` (consumed by
`exten_call.c` to dispatch the new `EXTEND` NFSv3 op — see
[chapter 6](./06-extend-op.md)).

## 2.8 Concurrency notes

The data structures in this chapter are touched from at least four
contexts: the fs_context parser (process context, mount syscall);
the SunRPC scheduler (`rpc_async_schedule` workqueue); the enfs
ping workqueue; and softirq via socket callbacks. The locking
model that holds it all together:

- **`xps->xps_lock`** (a spinlock) protects modifications to
  `xps_xprt_list` itself: insertions, deletions, the `xps_nxprts`
  / `xps_nactive` counters. Walks of the list are RCU.
- **`xps->xps_kref`** keeps the switch alive while a reader holds a
  reference. `xprt_switch_get` increments it; `xprt_switch_put`
  decrements; reaching zero invokes `xprt_switch_free` (RCU-deferred).
- **`xprt->kref`** keeps individual transports alive. enfs's
  `enfs_xprt_is_active` ([`enfs_roundrobin.c:30`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c))
  defensively reads `kref_read(&xprt->kref) <= 0` to skip xprts
  that are mid-destruction.
- **RCU read-sections** wrap every walk of `xps_xprt_list`. enfs's
  walks (`enfs_already_have_xprt`, `enfs_lb_find_next_entry_*`,
  the cursor-set helpers) all take `rcu_read_lock` /
  `rcu_read_unlock`. The cursor itself uses `smp_load_acquire`
  / `smp_store_release` semantics
  ([`enfs_roundrobin.c:47-50`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c))
  so concurrent iterator advances don't race.
- **`ctx->path_state`** is a simple `atomic_t`. Writers
  (`pm_set_path_state`) and readers (`pm_get_path_state`,
  `enfs_xprt_is_active`) coordinate purely through atomics, no
  lock. The "report" log message in `pm_set_path_state` reads the
  prior value once with `atomic_read` and writes the new value with
  `atomic_set`, so two concurrent transitions can race and produce
  misleading log output, but the persistent state is whatever the
  last `atomic_set` writes.
- **`ctx->queuelen`** is an `atomic_long_t`. Increments and
  decrements are matched at every `rpc_task_get_xprt` /
  `rpc_task_release_xprt` pair (per patch 0013). enfs's
  `enfs_dec_queuelen` ([`enfs_multipath.c:1008`](../../vendor/openeuler/fs/nfs/enfs/enfs_multipath.c))
  defensively logs and re-increments if the value goes negative,
  catching bookkeeping bugs without leaving the counter wrong.
- **The `ops` registration pointer** (`multipath_ops` in
  `sunrpc.ko`, `enfs_adapter` in `nfs.ko`) is an `__rcu` pointer
  set with `cmpxchg` from `module_init`/`module_exit`, read with
  `rcu_dereference` + `try_module_get` from every consumer. The
  `try_module_get` inside `rpc_multipath_ops_get` /
  `nfs_multipath_router_get` is the entire reason it is safe to
  unload `enfs.ko` while RPCs are in flight: any consumer holding
  the ops vector also holds a module reference, so the unload
  blocks until the last `*_put` call.

## 2.9 Running the tape forward: one RPC, end to end

Putting all the pieces together, here is a single READ on an
enfs-mounted file, in chronological order:

1. The application's `read(2)` reaches `nfs_read` and eventually
   constructs an `rpc_task_setup` and calls `rpc_run_task`
   ([`clnt.c:1224`](../../vendor/ubuntu-7.0/net/sunrpc/clnt.c)).
2. `rpc_new_task` allocates the task; `rpc_task_set_client`
   (line 1179) attaches the client and calls
   `rpc_task_set_transport` (line 1163).
3. The enfs hook at the top of `rpc_task_set_transport` (patched in
   by patch 0013) runs `enfs_set_transport`. For NFSv3 round-robin
   this is a no-op unless shard routing is enabled.
4. Stock `rpc_task_set_transport` falls through to
   `rpc_task_get_next_xprt(clnt)` (line 1175).
5. `rpc_task_get_next_xprt` calls `xprt_iter_get_next(&clnt->cl_xpi)`,
   which dereferences `xps_iter_ops` (or `xpi_ops` if set) and
   invokes the registered `xpi_next`.
6. Because this client was created with a non-NULL
   `multipath_option`, enfs's `create_clnt` callback ran during
   `rpc_create` and called `enfs_lb_set_policy`
   ([`enfs_roundrobin.c:264`](../../vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c)),
   which installed `enfs_xprt_iter_roundrobin` as
   `xps->xps_iter_ops`. So `xpi_next` resolves to
   `enfs_lb_iter_next_entry_roundrobin`.
7. That walks `xps_xprt_list` under RCU, skips xprts that aren't
   `enfs_xprt_is_active` (i.e., whose `path_state` isn't NORMAL or
   UNSTABLE), and picks the least-loaded xprt after the cursor.
8. The chosen xprt is returned up through `xprt_iter_get_helper`,
   which `xprt_get`s it and returns to `rpc_task_get_next_xprt`.
9. `rpc_task_get_xprt` increments `xps_queuelen` and `xprt->queuelen`
   (the stock counters), and the patched
   `rpc_multipath_ops_inc_queuelen` hook calls
   `enfs_inc_queuelen` to bump `ctx->queuelen` (the per-xprt
   counter the next iteration will consult).
10. `task->tk_xprt` is now bound. `rpc_execute` runs the task; the
    XDR encoder eventually writes the RPC header — patched by patch
    0013 to use `RPC_MULTIPAHT_UPDATE_RPC_PROC`, which lets enfs
    rewrite `cl_prog` / `cl_vers` for the EXTEND op
    ([chapter 6](./06-extend-op.md)) but leaves them alone for
    ordinary NFSv3 calls.
11. The transport sends the request; the reply comes back; the task
    completes; `xprt_release` runs; the patched
    `rpc_multipath_ops_xprt_iostat` and
    `rpc_multipath_ops_dec_queuelen` hooks update enfs's per-xprt
    statistics and decrement `ctx->queuelen`. The task is done; the
    next READ will pick a different xprt because the cursor advanced.

That ten-step trace exercises every cross-module entry point this
chapter has introduced. Subsequent chapters drill into the things
deliberately left as forward-references — what happens when step 11
returns an error (failover, [chapter 5](./05-failover.md)); what
happens before step 1 to make the rpc_clnt and switch exist at all
(mount, [chapter 3](./03-nfs-mount-flow.md)); and what `pm_ping`
does in the background to keep step 7's "is this xprt active?"
question answerable ([chapter 4](./04-pm-ping-state.md)).
