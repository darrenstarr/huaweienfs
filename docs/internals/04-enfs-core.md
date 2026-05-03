# Chapter 4 — The `enfs.ko` core

> **Where this fits.** Chapter 1 introduced the three-module shape of
> the port (`sunrpc.ko`, `nfs.ko`, `enfs.ko`). Chapter 2 walked through
> the build pipeline. Chapter 3 followed a `mount -t nfs -o
> enfs_info=...` from userspace through the parser hooks in `nfs.ko`
> down to the point where stock NFS would normally call into
> `rpc_create()`. This chapter picks up at the **moment a multipath
> client is needed at runtime** and walks through everything that
> happens inside the standalone `enfs.ko` module: how it loads, the
> vtables it publishes, the data structures that hold the per-mount
> path list, the function that turns "N remote IPs" into "N
> `rpc_xprt`s attached to one switch", the round-robin dispatcher, and
> the `/proc/enfs/` surface.

## 4.1 What's actually in `enfs.ko`

The Kbuild object list (`Kbuild` lines 158-178) tells us exactly what
the module contains. Eighteen translation units, one module:

```
fs/nfs/enfs/enfs_init.o            module_init / module_exit, vtable registration
fs/nfs/enfs/enfs_config.o          /etc/enfs/config.ini parser, config getters
fs/nfs/enfs/mgmt_init.o            two-line wrapper: kicks the config-reload timer
fs/nfs/enfs/enfs_multipath_client.o  per-nfs_client multipath state (alloc/free/match)
fs/nfs/enfs/enfs_multipath_parse.o   remoteaddrs=A~B~C / remoteaddrs=A-B parser
fs/nfs/enfs/failover_path.o        per-task failover (chapter 5)
fs/nfs/enfs/failover_time.o        per-task timeout adjust (chapter 5)
fs/nfs/enfs/enfs_roundrobin.o      load-balancer iter_ops (this chapter, §4.6)
fs/nfs/enfs/enfs_multipath.o       enfs_create_multi_xprt + ops vtable (this chapter)
fs/nfs/enfs/enfs_path.o            xprt context alloc/free helpers
fs/nfs/enfs/enfs_proc.o            /proc/enfs/<clnt>/{stat,path}
fs/nfs/enfs/enfs_remount.o         live `mount -o remount,enfs_info=...`
fs/nfs/enfs/pm_ping.o              path-manager liveness probe (chapter 5)
fs/nfs/enfs/pm_state.o             PM_STATE_* state machine (chapter 5)
fs/nfs/enfs/enfs_rpc_init.o        ENFS-private RPC program registration
fs/nfs/enfs/enfs_rpc_proc.o        ENFS-private RPC procedure dispatch
fs/nfs/enfs/exten_call.o           the NFSv3 EXTEND op caller side (chapter 6)
fs/nfs/enfs/dns_process.o          DNS-rebind worker
fs/nfs/enfs/enfs_lookup_cache.o    server-capability prober + DNS hostname cache
```

Three of those (`shard.o`, `enfs_test.o`, `shard_route.o`) are present
in `vendor/openeuler/fs/nfs/enfs/` but **deliberately omitted from the
build** — they pull in lockd-multipath surface we haven't ported yet.
Chapter 9 explains the compat-shim stubs (`shard_set_transport`,
`enfs_query_xprt_shard`, etc.) that let the rest of the code link
against a no-op shard layer.

`enfs.ko` depends on `nfs.ko` (for `enfs_adapter_register` /
`nfs_multipath_router_get`) and `sunrpc.ko` (for the
`rpc_multipath_ops` registry, plus the usual RPC API surface). It is
loadable on demand: the in-kernel hook
`enfs_parse_mount_options()` calls `request_module("enfs")` the first
time someone mounts with `enfs_info=...` (`vendor/openeuler/fs/nfs/
enfs_adapter.c:90-100`).

## 4.2 Module init order, and why it matters

`vendor/openeuler/fs/nfs/enfs/enfs_init.c:91` defines `init_enfs()`,
the `module_init` entry point. The order is load-bearing — get it
wrong and you crash on the first mount because some downstream callee
dereferences a NULL globals pointer.

```c
static int __init init_enfs(void)
{
    enfs_config_load();                               // 1
    ret = enfs_adapter_register(&enfs_adapter);       // 2 → into nfs.ko
    ret = init_helper_init(init_entry, ARRAY_SIZE(init_entry));
                                                      // 3 → 4 sub-inits below
    ret = enfs_rpc_init();                            // 4 → registers ENFS RPC prog
    return 0;
}
```

The four sub-inits in `init_entry[]` (line 84-89) are:

| order | name      | init                  | what it sets up |
|-------|-----------|-----------------------|-----------------|
| 1     | multipath | `enfs_multipath_init` | round-robin iter_ops, pm_ping workqueue + thread, /proc/enfs/, registers `rpc_multipath_ops` into sunrpc.ko |
| 2     | shard     | `enfs_shard_init`     | shard cache (stubbed in this build, see chapter 9) |
| 3     | mgmt      | `mgmt_init`           | starts the config-file reload timer |
| 4     | dns       | `enfs_dns_init`       | starts the DNS-rebind worker |

A few things follow from this order:

- **`enfs_config_load()` runs first** because every later init reads
  `enfs_get_config_*()`. With no `/etc/enfs/config.ini` installed,
  the static defaults from `enfs_config.c:40-51`
  (`DEFAULT_PATH_DETECT_INTERVAL = 10`, etc.) take effect — enfs.ko
  is fully functional out of the box.
- **`enfs_adapter_register()` runs before any sub-init.** Its
  vtable points at functions some of which need globals initialised
  in step 3 (e.g. `enfs_remount` needs the spinlocks from
  `enfs_multipath_init`). Safe in practice because nothing on the
  nfs.ko side calls in until the next mount, but a subtle correctness
  assumption with no rendezvous.
- **`rpc_multipath_ops_register(&ops)`** at
  `enfs_multipath.c:1083` is what tells sunrpc.ko to route per-task
  hooks through us. Until that runs, `rpc_multipath_ops_get()` returns
  NULL and every hook is a no-op (i.e. stock NFS behaviour). The
  transition is atomic — RCU-published via cmpxchg
  (`sunrpc_enfs_adapter.c:23-31`).

### What if `nfs.ko` isn't loaded?

`enfs.ko` has a module-symbol dependency on `enfs_adapter_register`
(in nfs.ko) and `rpc_multipath_ops_register` (in sunrpc.ko), so
`modprobe enfs` autoloads both first. `insmod` directly fails with
`Unknown symbol`. The reverse — `modprobe nfs` without enfs.ko —
works fine: the registry pointer is NULL and every nfs.ko callsite
guards with `if (ops == NULL) ...` and falls through to stock
behaviour.

## 4.3 The two adapter vtables

There are **two separate vtables**, one per direction of the dance.
Don't conflate them; they live in different modules and serve
different purposes.

### `enfs_adapter_ops` — `nfs.ko` calls into `enfs.ko`

Defined at `enfs_init.c:26-39`. Contains exactly those operations
that the NFS-client glue in nfs.ko needs to delegate to the multipath
implementation: parse `enfs_info=`, allocate / free / match the
per-`nfs_client` info, walk `/proc/mounts` printing, live remount,
and `enfs_trigger_get_capability`. Registered via
`enfs_adapter_register()` whose implementation lives in
`vendor/openeuler/fs/nfs/enfs_adapter.c:23` (i.e. in nfs.ko).

The accessor on the nfs.ko side is `nfs_multipath_router_get()` (same
file, line 47). It RCU-derefs the published pointer and `try_module_get()`s
enfs.ko's reference count. Every call site pairs `_get()` with
`_put()` so enfs.ko cannot be `rmmod`'d while a hook is in flight.

### `rpc_multipath_ops` — `sunrpc.ko` calls into `enfs.ko`

Defined at `enfs_multipath.c:1035-1051`. This vtable is consumed by
the per-task hook points patched into stock SunRPC by patch series
0013-0014. The fields cover the entire RPC-task lifecycle:

- `create_clnt` → `enfs_create_multi_xprt` (the big one — §4.5)
- `releas_clnt` → `enfs_release_rpc_clnt`
- `create_xprt` / `destroy_xprt` → alloc/free of the
  `enfs_xprt_context` reservation slot
- `init_task_req` → `failover_init_task_req` (chapter 5)
- `prepare_transmit` → `failover_prepare_transmit` (chapter 5)
- `failover_handle` → `failover_handle` (chapter 5)
- `set_transport` → `enfs_set_transport` (the per-task xprt picker
  for shard-based routing; round-robin doesn't go through here)
- `inc_queuelen` / `dec_queuelen` → maintains the per-xprt
  `queuelen` counter that the round-robin dispatcher consults

Registered via `rpc_multipath_ops_register()` from
`sunrpc_enfs_adapter.c:22`. Same RCU + cmpxchg pattern.

## 4.4 The multipath data structures

Two structs hold the multipath state, and it's worth being precise
about which one lives where.

### `multipath_mount_options` — short-lived, mount-time

Defined at `enfs_multipath_parse.h:10-17`:

```c
struct multipath_mount_options {
    int                          version;
    struct nfs_ip_list          *remote_ip_list;
    struct nfs_ip_list          *local_ip_list;
    struct enfs_route_dns_info  *pRemoteDnsInfo;
    u32                          fill_local;
    u32                          reserve[2];
};
```

This is what the `enfs_info=` parser produces. It lives on the
`nfs_fs_context` for the duration of the mount call, gets handed to
`enfs_create_multi_xprt` to drive transport creation, and is then
**copied** into the longer-lived per-client structure (next
paragraph) before being freed.

- `remote_ip_list`: up to `link_count_per_mount` (default 32, max
  1024 — `enfs.h:21`) IPv4 or IPv6 addresses. Mixing families on the
  same list is rejected at parse time
  (`enfs_multipath_parse.c:69-72`).
- `local_ip_list`: up to 8 source addresses
  (`MAX_SUPPORTED_LOCAL_IP_COUNT`, `enfs.h:20`). Empty means "let
  the kernel pick the source address per route".
- `pRemoteDnsInfo`: alternative to `remote_ip_list` — up to
  `MAX_DNS_SUPPORTED = 2` hostnames that get re-resolved by the DNS
  worker. Mixing IPs and DNS names is rejected.
- `fill_local`: tri-state; 1 means "user gave no localaddrs, please
  enumerate local NICs at mount time".

### `multipath_client_info` — long-lived, per-`nfs_client`

Defined at `enfs_multipath_client.h:10-21`:

```c
struct multipath_client_info {
    int                          version;
    struct work_struct           work;
    struct nfs_ip_list          *remote_ip_list;
    struct nfs_ip_list          *local_ip_list;
    struct enfs_route_dns_info  *pRemoteDnsInfo;
    s64                          client_id;
    u32                          fill_local : 1;
    u32                          updating_domain : 1;
    u32                          reverse[2];
};
```

A pointer to this struct lives in `nfs_client->cl_multipath_data`
(field added by patch 0007). `nfs_multipath_client_info_init`
(`enfs_multipath_client.c:147`) deep-copies the parser output into a
fresh `multipath_client_info`. `nfs_multipath_client_info_match`
(line 243) is what makes "two mounts of the same export with the same
addrs share an nfs_client" work — same hash key as stock plus an
address-set comparison.

The lifetime distinction matters for one concrete reason: **DNS
re-resolution**. The DNS worker (`dns_process.c`) walks
`cl_multipath_data->pRemoteDnsInfo`, re-resolves each name, and
updates `remote_ip_list` in the *client_info*. The original
*mount_options* is long gone. That's why `print_dns_info` in
`enfs_multipath_client.c:343-362` reads from the
`multipath_client_info`, not the mount struct, when rendering
`/proc/mounts`.

## 4.5 `enfs_create_multi_xprt`: from N IPs to N transports

This is the heart of the multipath setup path. Stock NFS calls
`rpc_create()` once per nfs_client and gets back an `rpc_clnt` with
exactly one `rpc_xprt`. With enfs in the picture, sunrpc.ko's
`rpc_create()` (patched at `0013`) calls
`rpc_multipath_ops_create_clnt(args, clnt)` immediately afterwards,
which lands in `enfs_create_multi_xprt` at
`enfs_multipath.c:919`.

```c
void enfs_create_multi_xprt(struct rpc_create_args *args, struct rpc_clnt *clnt)
{
    if (args->version == 4)
        return;                                          // line 925-926: NFSv4 unsupported
    if (!enfs_mount_count_add(1)) return;                // line 929: global mount cap
    if (!enfs_link_count_add(1))  goto cleanup_mount;    // line 934: global xprt cap
    cargs  = kmalloc(sizeof(*cargs), GFP_KERNEL);        // copy args; will outlive caller
    thargs = kmalloc(sizeof(*thargs), GFP_KERNEL);
    alloc_main_xprt_multicontext(args, clnt);            // line 949: tag the original xprt
    thargs->args = cargs; thargs->clnt = clnt; thargs->data = args->multipath_option;
    err = enfs_multipath_create_thread(thargs);          // line 955: do the work
    ...
}
```

A few things to flag:

- **NFSv4 short-circuit at line 925.** This build does multipath for
  NFSv3 only; chapter 8 covers what was deferred and why.
- **Global caps.** `enfs_mount_count_add` (line 127) and
  `enfs_link_count_add` (line 69) refuse to push past
  `ENFS_MAX_MOUNT_COUNT = 256` (`enfs.h:33`) and the configurable
  `link_count_total` (default 512, max 16384 — `enfs.h:29-31`).
- **`alloc_main_xprt_multicontext`** (line 899) doesn't *create* a
  transport — the original xprt from `rpc_create` already exists.
  It wires an `enfs_xprt_context` reservation slot onto it via
  `xprt_set_reserve_context`, marks `ctx->main = true`, and pushes
  it to `PM_STATE_NORMAL` (the only xprt that starts in NORMAL
  rather than INIT). The "main" xprt is the original
  `server:/export` address; multipath xprts come from `remoteaddrs=`.
- **Despite the name, `enfs_multipath_create_thread`** (line 823)
  runs synchronously on the caller's stack — OE-vestigial naming.
  The mount syscall blocks until all transports are constructed
  and probed.

The heavy lifting is in `enfs_xprt_ippair_create` (line 638) →
`enfs_combine_addr` (line 493) or `enfs_combine_addr_with_no_local`
(line 575). The choice (line 647-654):

```c
if (xprtargs->ident == XPRT_TRANSPORT_RDMA ||
    mopt->local_ip_list->count == 0)
    enfs_combine_addr_with_no_local(...);   // 1 local × N remote = N xprts
else
    enfs_combine_addr(...);                 // M local × N remote = M*N xprts (LCM-ordered)
```

`enfs_combine_addr` walks the cartesian product of (local × remote)
in an order picked to maximise spread when M and N share a common
factor (line 528-536; LCM offsets the remote index per cycle so a
2×4 layout actually rotates rather than pairing local[0] with the
same remote each round).

For each pair it calls `enfs_configure_xprt_to_clnt` (line 315) which
copies the addresses into the `xprt_create` template and invokes
`rpc_clnt_add_xprt(clnt, xprtargs, enfs_add_xprt_setup, attach_info)`
(line 332). `enfs_add_xprt_setup` (line 283) runs once SunRPC has
built the xprt:

```c
ctx = xprt_get_reserve_context(xprt);
memset(ctx, 0, sizeof(struct enfs_xprt_context));
ctx->stats    = rpc_alloc_iostats(clnt);
ctx->main     = false;
ctx->protocol = attach_info->protocol;
if (srcaddr) ctx->srcaddr = *srcaddr;
pm_set_path_state(xprt, PM_STATE_INIT);
pm_ping_set_path_check_state(xprt, PM_CHECK_INIT);
attach_info->xprt = xprt;
xprt_get(xprt);
ret = pm_ping_rpc_test_xprt_with_callback(clnt, xprt, pm_xprt_ping_callback, attach_info->data);
return 1;   // tell rpc_clnt_add_xprt: we'll attach to xps ourselves later
```

The `return 1` tells `rpc_clnt_add_xprt` *not* to do the default
`xprt_switch_add_xprt` — we want to attach only after probing and
seeing what state the path comes up in. Switch attach is deferred to
`enfs_add_xprts_to_clnt` (line 440) which runs after
`wait_event(...wait_queue_condition == 0)` at line 566 — i.e. after
all per-xprt ping callbacks have completed.

This synchronous probe-then-attach pattern means a 4-IP mount with
one dead address waits the full `path_detect_timeout` (default 5 s)
before `mount` returns. The dead address still goes into the switch
in `PM_STATE_FAULT` (line 475-486 — any connected state, or any xprt
at all if `create_path_no_route` is set, gets attached). The
dispatcher then skips it until pm_ping promotes it (chapter 5).

Right before the function returns, line 871 sets the load-bearing
flag:

```c
create_args->clnt->cl_enfs = 1;
```

This single bit is what every other piece of the system uses to
distinguish "an enfs-managed clnt" from "a stock NFS clnt". §4.7
enumerates every site that branches on it.

## 4.6 Round-robin: plugging into `rpc_xprt_iter`

`enfs_roundrobin.c` plugs into the SunRPC iterator machinery by
publishing two `struct rpc_xprt_iter_ops` vtables (lines 272-282):

```c
static const struct rpc_xprt_iter_ops enfs_xprt_iter_roundrobin = {
    .xpi_rewind = enfs_lb_iter_default_rewind,
    .xpi_xprt   = enfs_lb_iter_current_entry,
    .xpi_next   = enfs_lb_iter_next_entry_roundrobin,
};
static const struct rpc_xprt_iter_ops enfs_xprt_iter_singular = {
    .xpi_rewind = enfs_lb_iter_default_rewind,
    .xpi_xprt   = enfs_lb_iter_current_entry,
    .xpi_next   = enfs_lb_iter_next_entry_sigular,
};
```

The switch from stock to enfs iter ops happens in
`enfs_lb_switch_set_roundrobin` (line 216), which is called from
`enfs_lb_set_policy` (line 264) at the very end of
`enfs_xprt_ippair_create` (line 656). The patched
`rpc_xprt_switch->xps_iter_ops` field
(added by patch 0014) is overwritten with `&enfs_xprt_iter_roundrobin`
for v3 clients and with `&enfs_xprt_iter_singular` for v4 (which we
don't currently exercise — see §4.5 caveat).

### What "next" actually does

The interesting one is `enfs_lb_find_next_entry_roundrobin` (line 53).
It is **not a simple pick-the-next-pointer-in-the-list**; it is a
combination of round-robin advancement and per-xprt queue-length
balancing. Walking the loop:

```c
list_for_each_entry_rcu(pos, &xps->xps_xprt_list, xprt_switch) {
    if (!nativeLinkStatus && enfs_is_main_xprt(pos))      // optional skip
        continue;
    if (!enfs_xprt_is_active(pos)) {                      // PM_STATE_NORMAL or UNSTABLE
        prev = pos;
        continue;
    }
    ctx = xprt_get_reserve_context(pos);
    pos_xprt_queuelen = atomic_long_read(&ctx->queuelen);
    if (min_queuelen_xprt == NULL || pos_xprt_queuelen < min_xprt_queuelen) {
        min_queuelen_xprt = pos;                          // track absolute minimum
        min_xprt_queuelen = pos_xprt_queuelen;
    }
    if (cur == prev) found = true;                        // we've passed the cursor
    if (found && (optimal_xprt == NULL ||
                  optimal_queuelen < min_xprt_queuelen)) {
        if (min_xprt_queuelen == 0)
            return pos;                                   // idle path — take it
        optimal_xprt = pos;                               // best after cursor
        optimal_queuelen = pos_xprt_queuelen;
    }
    prev = pos;
}
return optimal_xprt ? optimal_xprt : min_queuelen_xprt;
```

So the algorithm is:

1. Skip the "main" xprt when `native_link_io_enable=0` (the OE
   pattern that says "don't send IO over the original mount address,
   only over the multipath set"; default on).
2. Skip dead paths.
3. Among the live ones *after the current cursor*, pick the one with
   fewest in-flight requests (zero if available).
4. If no live xprt sits after the cursor, fall back to the global
   minimum.

The `queuelen` per-xprt counter is the field of
`enfs_xprt_context` that `inc_queuelen` / `dec_queuelen` hooks
maintain. Each task that goes out the door bumps it up; each
completed task bumps it down (`enfs_multipath.c:996` and `:1008`).

### When does `xpi_next` actually fire?

Not on every send — only on every *fresh* RPC task. The cursor
advances exactly once per call to `rpc_task_get_next_xprt(clnt)`
(implemented in our patched `clnt.c:1157`, exported by patch 0016).
The two caller paths are:

- The normal `rpc_run_task` → `call_reserveresult` →
  `task->tk_xprt = rpc_task_get_next_xprt(clnt)` chain in stock
  SunRPC — i.e. once per RPC.
- The `failover_retry_path` reroute (`failover_path.c:37`) when a
  task is being re-aimed at a different xprt after a timeout. See
  chapter 5.

A task that retries the same RPC over the same xprt (e.g. soft
timeout, EAGAIN reconnect) does *not* re-pick. The cursor is per
`rpc_clnt`, stored in `cl_xpi.xpi_cursor`, and only advanced by the
two paths above.

### The "singular" iter

Used for NFSv4 paths. Returns the same xprt on every call, because
NFSv4 sessions are stateful at the single-server level and you can't
just re-aim at a different server mid-session. Until v4 multipath
arrives, this iter is what guarantees an enfs-tagged v4 client
behaves identically to a stock one — it just locks the cursor onto
the first active xprt and never moves it.

## 4.7 `cl_enfs == 1` — every site that branches on it

The `cl_enfs:1` bitfield was added to `struct rpc_clnt` by patch
0005. It is the single global check for "is this client multipath?".
Grep across the source tree shows ten read sites and one write site:

| file:line | what it gates |
|---|---|
| `enfs_multipath.c:871` | **The only writer.** Set to 1 inside `enfs_multipath_create_thread` after the xprts are built. |
| `failover_com.h:22` | `failover_is_enfs_clnt(clnt)` walks `cl_parent` chain to the root and returns `target->cl_enfs == 1`. Used by every failover hook to ignore stock clients. |
| `failover_path.c:266` | `failover_reselect_transport` only crosses to the parent's xprt cursor for v4 if the parent is enfs-tagged. |
| `enfs_roundrobin.c:266` | `enfs_lb_set_policy` swaps in our iter_ops only on enfs clients. |
| `enfs_roundrobin.c:334` | `enfs_lb_revert_policy` (called on module exit) reverts iter_ops only on enfs clients. |
| `pm_ping.c:436` | The pm_ping kthread walks `sn->all_clients` and only pings enfs ones. |
| `enfs_proc.c:498` | `enfs_proc_delete_clnt` only removes the per-clnt /proc tree if it was created. |
| `enfs_proc.c:588` | `enfs_proc_init_create_clnt` creates the per-clnt /proc tree only for enfs clients. |
| `enfs_proc.c:597` | `enfs_proc_destroy_clnt` (module exit) symmetrical. |
| `sunrpc_enfs_adapter.c:219` | `rpc_clnt_has_multipath()` exported helper. |
| `enfs_proc.c:100` | Debug log line — printed unconditionally in `ifdebug(ENFS)`. |

Semantically, `cl_enfs == 1` means: *the round-robin iter is in
place, the failover hooks should fire, the pm_ping thread should
probe my xprts, and I have a `/proc/enfs/<addr>_<id>/` directory*.
It does **not** mean "I have more than one xprt" — a one-IP
multipath mount is still `cl_enfs == 1` and still goes through all
the hook machinery. That is by design; live remount can grow a
single-xprt mount into a multi-xprt one without flipping the bit.

## 4.8 `/proc/enfs/` for the SRE

Two files per active enfs client, plus a top-level help file. The
client name is `<peer-ip>_<cl_clid>` — see `clnt_proc_name`
(`enfs_proc.c:416`).

```text
/proc/enfs/
├── 192.0.2.10_8/
│   ├── stat       per-xprt R/W counts, RTT, exec time, queuelen
│   └── path       per-xprt local→remote address, PM state, XPRT state bits
└── 192.0.2.10_9/
    ├── stat
    └── path
```

### `stat`

Tab-separated columns (`enfs_proc.c:320-323`):

```
id   local_addr   remote_addr   r_count  r_rtt  r_exec  w_count  w_rtt  w_exec  queuelen
```

`r_*` and `w_*` come from `clnt->cl_procinfo` for the READ and WRITE
NFSv3 ops only — the filter is hard-coded in `should_print()` (line
59). `r_rtt` is mean network RTT in milliseconds; `r_exec` is mean
end-to-end execution time. `queuelen` is the live atomic that the
round-robin dispatcher reads. **Writing the literal string `reset`
to `stat` zeros all per-xprt counters** (`enfs_proc_write`, line
368) — useful for a "from now" measurement in a benchmark.

### `path`

Tab-separated columns (`enfs_proc.c:295-298`):

```
id   local_addr   remote_addr   path_state   xprt_state
```

`path_state` is the enfs PM state — `Init`, `Normal`, `Unstable`,
`Fault`, or `Unknown` (`pm_state.c:144-160`). `xprt_state` is the
SunRPC `xprt->state` bitmask rendered as `LOCKED|CONNECTED|...`
(`pm_state.c:168-216`).

This is the file you `cat` to answer "is path 3 down?". An SRE
runbook entry: a path showing `Normal` here but `Fault` would mean
the application sees flawless service via the live paths and the
dispatcher is correctly skipping the bad one; a path showing
`Fault` here means it has been DOWN since at least the last pm_ping
cycle. Latency to detection is bounded by `path_detect_interval`
(default 10 seconds — `enfs_config.c:40`).

### Top-level `shardview` (compile-time gated)

Conditional on `NFS_CLIENT_DEBUG` (`enfs_proc.c:558-574`).
`shardview` is a write-only debug interface for triggering EXTEND-op
queries (chapter 6). Not present in release builds.

## 4.9 `enfs_lookup_cache.c` — what it actually caches

The name is misleading. This file does **not** cache directory
lookups. It does two things:

1. **Server-capability prober.** A kthread (`lookupcache_routine`,
   line 422) walks every NFS volume on a periodic timer (default 60
   seconds, `lookupcache_interval`) and issues an ENFS-private RPC
   (`ENFSPROC_LOOKUPCACHE`, registered via `enfs_proc_reg` in
   `enfs_lookupcache_init`) to ask "what `lookupcache=` mode does
   the server prefer?". The reply (`lookupCache` field of
   `enfs_get_onfig_res`) maps to `ENFS_LOOKUPCACHE_{ALL,NONEG,NONE}`
   and the corresponding `enfs_flags` bits are set on the
   `nfs_server` (lines 121-141). The user's `mount -o lookupcache=`
   choice still wins; the server hint is consulted only if the user
   said "all".

2. **`enfs_trigger_get_capability`.** Called from `nfs.ko` when
   stock super.c needs to know "should I disable negative-dentry
   caching?" (patched call site is in `super.c`, the function is
   exported through the `enfs_adapter_ops.trigger_get_capability`
   slot at `enfs_init.c:38`). It enqueues a one-shot work that runs
   the same prober logic for that one server.

So the practical claim "DNS-resolved hostnames don't pay DNS RTT on
reconnect" is not in this file — that lives in `dns_process.c`
(covered in chapter 7 with the locking model). What `enfs_lookup_cache.c`
caches is the **server's lookup-cache preference**, expressed as
flag bits on the `nfs_server`. Naming is unfortunate; the OE
nomenclature is what it is.

---

**Next:** chapter 5 picks up the path-manager subsystem
(`pm_ping.c`, `pm_state.c`, `failover_path.c`) and walks through
exactly what happens to in-flight RPCs when one of those four
transports stops responding.
