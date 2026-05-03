# 7. Locking, concurrency, and refcount hygiene

> Prerequisites: chapters [01-architecture](./01-architecture.md),
> [02-rpc-multipath](./02-rpc-multipath.md), and
> [03-nfs-mount-flow](./03-nfs-mount-flow.md). Chapter 5 (failover) is
> useful background but not strictly required.

This chapter covers the synchronisation rules that hold the multipath
subsystem together: how the `rpc_xprt_switch` is published to readers,
when refcounts are taken and dropped, where the line sits between
"lock-held" and "lockless" helpers, and the `__GENKSYMS__` trick that
lets a struct grow new fields without breaking unmodified consumers.
The chapter does not introduce new code — it explains the rules that
chapters 1–6 take for granted.

## 7.1 The high-level rule

The xprt-switch is the per-client multipath set: a list of
`rpc_xprt`s plus an iterator (round-robin or singular). Three
disciplines apply in concert:

- **Readers** walk the switch under `rcu_read_lock()`. The list is
  spliced via `list_del_rcu` / `list_add_tail_rcu`
  (`vendor/ubuntu-7.0/net/sunrpc/xprtmultipath.c:75`).
- **Mutators** (add xprt, remove xprt, change iter ops) take the
  per-switch spinlock `xps->xps_lock` before mutating. See
  `xprt_switch_add_xprt_locked` / `xprt_switch_remove_xprt_locked`
  at `vendor/ubuntu-7.0/net/sunrpc/xprtmultipath.c:32-77`.
- **Lifetime** is governed by `kref` (`xps_kref`); teardown runs only
  when the last reference goes away
  (`xprt_switch_free`, `:188-192`).

Why RCU rather than a reader-writer spinlock? The hot path —
"select the next xprt for an outgoing RPC" — runs on every NFS
read/write/getattr. Mutation is rare: mount, remount, failover. RCU
makes the read side free of atomic operations; the cost shifts to
the writer's publication step and a deferred reclaim. For sustained
NFS I/O with admin edits at minutes-or-shift cadence, that is the
right trade-off.

## 7.2 The kref dance

`xps_kref` counts everything that holds the switch alive. The wrappers
`xprt_switch_get` / `xprt_switch_put` (exported by patch 0017, see
[chapter 8](./08-patch-series.md#patch-0017)) are the only sane way
to bump or drop the count:

```c
/* vendor/ubuntu-7.0/net/sunrpc/xprtmultipath.c:205 */
struct rpc_xprt_switch *xprt_switch_get(struct rpc_xprt_switch *xps)
{
    if (xps != NULL && kref_get_unless_zero(&xps->xps_kref))
        return xps;
    return NULL;
}
```

`kref_get_unless_zero` is the load-bearing detail. A reader can be
mid-RCU-walk on a pointer whose count is already on its way to zero
(the writer dropped the last reference between the reader's
`rcu_dereference` and its `xprt_switch_get`). In that case
`kref_get_unless_zero` returns false and the caller bails. RCU keeps
the memory alive long enough for the test to be safe; the kref
ensures the winning bumper has the only remaining reference.

Where each event fires:

- **Mount.** `rpc_create()` builds the initial switch. enfs's adapter
  bumps once per remote address added; each bump pairs with an
  `xprt_switch_put` at unmount.
- **Per RPC task lifetime.** A task gets an *xprt* reference (via
  `xprt_get`) when `rpc_task_set_transport()` runs, dropped in
  `xprt_release()` (exported by patch 0018). The task does *not*
  hold a switch reference; it reaches the switch via
  `clnt->cl_xpi.xpi_xpswitch` under RCU.
- **pm_ping work items** (`pm_ping.c:392`) capture an `rpc_xprt *`
  for the duration of the ping but do not hold a switch reference —
  the switch outlives the workqueue by construction (the workqueue
  is destroyed before the client refcount can hit zero).
- **Unmount.** `rpc_release_client` walks the xpi and drops the
  switch reference; the kref reaches zero only when every other
  holder (per-xprt refs, in-flight failover work) has also let go.

If kref balance is wrong, the symptom is either a use-after-free in
slabinfo (rare — RCU defers the free past a grace period and the
re-check catches most racy cases) or a leak that lingers after the
mount is gone. Rule for any new code path that escapes the original
task lifetime: dereference under `rcu_read_lock`, then
`xprt_switch_get` if you need the pointer beyond the read-side
critical section. enfs's failover code follows this at every site.

## 7.3 RCU on the read side

`rpc_xprt_iter_*` is the RCU-only iterator API. Every public read-side
helper that walks the iterator either documents that the caller must
hold `rcu_read_lock()`, or takes it itself for the duration:

```c
/* vendor/ubuntu-7.0/net/sunrpc/xprtmultipath.c:603 */
/*
 * Caller must be holding rcu_read_lock().
 */

/* :637 */
rcu_read_lock();
... walk ...
rcu_read_unlock();
```

enfs's code follows the same convention. `enfs_multipath.c:393`
takes the read lock around the `rcu_dereference(clnt->cl_xpi.xpi_xpswitch)`
that fetches the switch, then bumps `xprt_switch_get` *inside* the
read-side critical section before letting go of `rcu_read_lock()`.
That is the only safe ordering: dereference under RCU, bump kref, then
exit RCU.

Why RCU here and not a spinlock? Two reasons:

1. The read path is on every RPC submission. A spinlock would be a
   hot contention point, and worse: the reader sometimes holds the
   pointer across operations that can sleep (e.g. queueing the RPC
   into a workqueue). Sleeping under a spinlock is a kernel BUG.
2. Mutation is so rare that the writer's RCU publication overhead
   (one `smp_wmb` plus one synchronize_rcu in the eventual reclaim
   path) is invisible at workload scale. A mount edits the switch
   once; failover edits it on the order of seconds, not microseconds.

## 7.4 The `__GENKSYMS__` / CRC trick

Patches 0005 and 0007 (see [chapter 8](./08-patch-series.md)) add new
fields to `struct rpc_clnt`, `struct rpc_create_args`, and
`struct nfs_client` / `struct nfs_server`. Adding fields to those
structs would normally break every other module that includes the
matching header — because the kernel's symbol-versioning scheme
(`genksyms`) computes a CRC for every exported symbol whose signature
mentions the changed struct, and those CRCs are baked into the
*consumer* module's `vermagic` table.

`lockd.ko`, `nfs_acl.ko`, `nfsv3.ko`, and `nfsd.ko` all link against
`sunrpc.ko`'s `EXPORT_SYMBOL_GPL`s for things like `rpc_create()` and
`rpc_clnt_*`. Their CRCs were computed against the *stock* layout of
`struct rpc_clnt`. Drop in our patched `sunrpc.ko` with an extended
layout and the load-time `module: disagrees about version of symbol
rpc_create` error fires for every consumer. The DKMS package would
ship a `sunrpc.ko` that the rest of the kernel refuses to talk to.

The trick (taken verbatim from OpenEuler's tree) is to make the
*compiler* see the extended layout, but make `genksyms` see the stock
layout, by gating the new fields with a preprocessor symbol that
genksyms defines and gcc does not:

```c
/* patch 0005, applied to include/linux/sunrpc/clnt.h */
#if defined(__GENKSYMS__) || !IS_ENABLED(CONFIG_SUNRPC_ENFS)
        cl_shutdown : 1,
#else
        cl_shutdown : 1,
        cl_enfs     : 1,
#endif

...

#if !defined(__GENKSYMS__) && IS_ENABLED(CONFIG_SUNRPC_ENFS)
    void *multipath_option;
#endif
```

When `genksyms` runs to compute CRCs, `__GENKSYMS__` is defined, the
`#if defined(__GENKSYMS__) || ...` arm is taken, and the struct it
sees is byte-identical to stock. CRC for `rpc_create` is unchanged.
When gcc compiles the same header during the actual build,
`__GENKSYMS__` is *not* defined and `CONFIG_SUNRPC_ENFS=1`, so the
extended arm is taken and the struct grows the new fields. Memory
layout has the new slots; the symbol version table claims it's the
stock struct.

This is fragile in exactly one direction: any `EXPORT_SYMBOL`ed
function in our patched `sunrpc.ko` whose signature touches one of
the extended fields by *value* (not by pointer) would compute a
different CRC under both views, defeating the trick. None of the
exports we add (chapters 0016–0018, 0020) take or return
`struct rpc_clnt` by value — they all take pointers. Pointer-to-struct
arguments are CRC-equivalent under both views because the pointee's
layout doesn't enter the CRC.

Net effect: stock `lockd.ko`, `nfs_acl.ko`, `nfsd.ko` (whatever
ships with the user's kernel package) load against our patched
`sunrpc.ko` without `disagrees about version of symbol`. They
continue to operate on the stock-shaped slice of the struct; the
extended slice is touched only by our own `nfs.ko` and `enfs.ko`,
both of which were built against the patched headers.

This is *the* load-bearing trick of the whole DKMS approach. Without
it, "DKMS replacement of sunrpc.ko" would not be a viable shipping
strategy and the project would have to either (a) rebuild every
consumer module (which means vendoring `lockd`, `nfs_acl`, `nfsd`)
or (b) package as a kernel patch rather than DKMS. The former is what
the Kbuild already does for `lockd.ko` and `nfs_acl.ko` (because they
have other CRC dependencies on `sunrpc.ko` symbols that the trick does
*not* protect — see `Kbuild` lines 134-152). The latter is the option
we did not take.

## 7.5 Workqueues

enfs uses three workqueues, all created with `create_workqueue()`
(non-bounded, non-ordered, unfreezable):

| Workqueue | Owner | Purpose | Init / fini |
|---|---|---|---|
| `pm_ping_workqueue` | `pm_ping.c` | periodic NULL-RPC liveness probes against each xprt in a multipath set | `pm_ping_workqueue_init` (`pm_ping.c:492-501`), torn down in `pm_ping_workqueue_fini` (`:504-510`) |
| `enfs_lookupcache_workqueue` | `enfs_lookup_cache.c` | refresh the per-server lookup-cache mode after a capability probe completes | `enfs_lookupcache_workqueue_init` (`enfs_lookup_cache.c:453-461`) |
| `enfs_dns_workqueue` | `dns_process.c` | re-resolve hostnames for `remoteaddrs=` entries that were specified as DNS names; reconcile the live xprt-switch against the new IP set | `enfs_dns_workqueue_init` (`dns_process.c:925-934`) |

Why workqueues for this work and not timers or kthreads?

- **Timers are the wrong primitive** because the work is not a tiny
  bookkeeping action — a ping involves submitting an RPC and waiting
  for a callback. Doing that from a softirq-context timer callback
  would either block the timer (forbidden) or require trampolining
  back to a sleepable context anyway. Workqueues *are* that
  sleepable context, with built-in trampolining.
- **Kthreads would be heavier than necessary.** A kthread is a
  full kernel thread with its own stack, scheduled directly by the
  kernel. For periodic, mostly-idle work, that's wasteful — a
  workqueue worker is shared across many work items. The pm_ping
  subsystem does have one supporting kthread (`pm_ping_routine` at
  `pm_ping.c:467`) that loops on a wait condition and submits new
  work items into the workqueue when the condition fires; the
  kthread itself does almost no work.
- **Workqueues let us cancel pending work cleanly** at unmount.
  `destroy_workqueue` flushes pending items and waits for in-flight
  ones to complete before returning, which gives us a well-defined
  shutdown order: stop accepting new work, drain in-flight, free.

The DNS workqueue is the most interesting of the three because its
work items live longer than a typical RPC: they kick off a name
resolution (which can block for seconds in pathological cases), then
mutate the xprt-switch via the locked helpers from §7.1. The DNS
worker is the canonical example of "writer side of the
RCU/spinlock/kref dance" — it reads the switch under RCU to enumerate
current xprts, takes `xps->xps_lock` to add or remove paths, and
drops a refcount on each retired xprt via `xprt_switch_put` only
after all readers have observed the new list (via the implicit
`synchronize_rcu` inside the RCU-list helpers).

## 7.6 The `cl_enfs` bitfield: why a bit, not a flag word

Patch 0005 adds `cl_enfs : 1` (a single bit) to `struct rpc_clnt`,
co-located with the existing bits `cl_noretranstimeo`, `cl_autobind`,
`cl_chatty`, `cl_shutdown`, and `cl_netunreach_fatal`. It is read on
every RPC submission ([chapter 5](./05-failover.md) covers the call
sites; the relevant test is `if (clnt->cl_enfs)` at sites like
`pm_ping.c:436`, `enfs_roundrobin.c:266`).

Why a bit and not, say, a `flags` word?

- **Branch cost.** The hot path tests this once per RPC. A bitfield
  test compiles to a single `test`/`bt`-class instruction against a
  cache line that is already hot (the rest of `rpc_clnt` was just
  dereferenced to get to it). A separate `unsigned long flags` word
  would not be cheaper but would be a separate slot, slightly
  enlarging the struct.
- **Allocation.** The bitfield word that already holds
  `cl_shutdown`, etc., has free bits. Adding `cl_enfs` consumes one
  of them with zero memory overhead. A new flag word would cost 8
  bytes (with alignment).
- **Symbolic atomicity is not needed.** The bit is set exactly once,
  by `enfs_multipath.c:871`, before the client is published to any
  reader. After that point it is read-only for the lifetime of the
  client. There is no read-modify-write race to protect against, so
  the lack of `set_bit` / `test_bit` atomicity (you can't atomically
  manipulate a single bit inside a shared bitfield word — you have
  to RMW the whole word) is a non-issue.

If a future enfs feature ever needs to flip the bit at runtime, that
calculus would need to be revisited; for now, single-bit, set-once,
read-many is the right shape.

## 7.7 "Locked" vs. "lockless" helpers

The xprt-switch API has a deliberate split between two layers:

- `xprt_switch_add_xprt_locked(xps, xprt)` — caller already holds
  `xps->xps_lock`. Just appends the xprt to the list and increments
  counters. Defined at `vendor/ubuntu-7.0/net/sunrpc/xprtmultipath.c:32`.
- `rpc_xprt_switch_add_xprt(xps, xprt)` — the public wrapper. Takes
  the lock, calls the locked helper, releases the lock. Defined at
  `vendor/ubuntu-7.0/net/sunrpc/xprtmultipath.c:53`.

The same shape exists for remove (`xprt_switch_remove_xprt_locked` vs
`rpc_xprt_switch_remove_xprt`).

Patch 0017 ([chapter 8](./08-patch-series.md#patch-0017)) exports
*both* the locked variant and the public wrapper. enfs's adapter code
sometimes already holds `xps_lock` for unrelated reasons — for
example, when enfs is in the middle of reconciling a parsed mount
option against an existing switch, it holds the lock across the whole
walk to keep the iteration consistent. In that case it must call
`xprt_switch_add_xprt_locked` directly; calling the public wrapper
would deadlock on the lock the caller already holds. The locked
helper is the API contract for "I already own the lock, just append".

If you find yourself adding a new code path that needs to mutate the
switch, the rule is: if you do not have a *very specific* reason to
already hold `xps_lock`, call the public wrapper. The locked variant
is reserved for the (rare) case where you need to do several
mutations as one logical unit and you want to keep the iteration
window closed.

## 7.8 RPC task scheduling and `tk_xprt`

A subtle concurrency point that comes up in failover: an RPC task
holds a pointer to its current transport in `task->tk_xprt`. During
normal operation this is set once (at `rpc_task_set_transport`) and
read on every step of the task's lifecycle. During failover, enfs
*rewrites* it mid-task — `failover_path.c:37` does
`task->tk_xprt = rpc_task_get_next_xprt(task->tk_client)`.

Why is this safe without an explicit lock around `tk_xprt`? Because
the rule for an RPC task is that, at any instant, it is on exactly
one of: (a) a wait queue inside SunRPC, (b) the run queue of an
`rpciod` workqueue worker, or (c) executing inside one of its own
ops callbacks (which themselves run on rpciod). The transitions
between these states are serialised by SunRPC's internal task
locking. enfs's failover code only mutates `tk_xprt` in case (a),
when the task is parked on a wait queue and not actively running on
any CPU. The wakeup that unparks the task happens *after* the
mutation, so the task sees the new `tk_xprt` from its first read.

There is no comment in `vendor/ubuntu-7.0/net/sunrpc/clnt.c` that
states this rule explicitly — it is implicit in the task-state
model that `rpc_execute` walks. If you are extending failover, the
practical guideline is: only touch `tk_xprt` from a context where
you have just dequeued the task and have not yet re-queued it. Any
other context is racy.

## 7.9 Summary

| Object | Read-side rule | Write-side rule |
|---|---|---|
| `clnt->cl_xpi.xpi_xpswitch` (the per-client xprt-switch pointer) | `rcu_read_lock()` + `rcu_dereference` | `rcu_assign_pointer` (rare; only at switch swap) |
| switch's xprt list | `rcu_read_lock()` + `list_for_each_entry_rcu` | `xps->xps_lock` + `list_*_rcu` |
| `xps_kref` | `xprt_switch_get` (`kref_get_unless_zero`) | `xprt_switch_put` (`kref_put`) |
| `xprt` itself | `xprt_get` / pointer held by task | `xprt_put` / `xprt_release` (patch 0018) |
| `cl_enfs` bit | plain read; set-once at mount | written exactly once at `enfs_multipath.c:871` |
| `tk_xprt` | only when task is parked | failover: rewrite while task is on wait queue |

If any patch you write breaks any row in this table, expect a
use-after-free in `slabinfo` or a `disagrees about version of symbol`
on module load. Both are observable; neither is fun to debug after
the fact. The compat shims in [chapter 9](./09-compat-shims.md) and
the patch series in [chapter 8](./08-patch-series.md) preserve every
one of these invariants on purpose.
