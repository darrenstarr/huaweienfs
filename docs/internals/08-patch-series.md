# 8. The patch series

> Prerequisites: [chapter 1](./01-architecture.md) (the three-module
> stack) and [chapter 9](./09-compat-shims.md) (compat header) for
> cross-references. This chapter is structurally a reference — read
> top to bottom for context, then dip into individual patches as
> needed.

`patches/ubuntu-7.0/series` lists 20 numbered patches. They are
applied by `scripts/build-src-tree.sh` step 3 (chapter 1) on top of
the pristine Ubuntu 7.0 vendored sources. Each patch is also
described in long form under `docs/changes/NNN-*.md`; this chapter
distils those into a single-page reference.

The 20 patches divide into seven thematic clusters:

| Cluster | Patches | Theme |
|---|---|---|
| Build wiring | 0001–0004 | Makefile and Kconfig stanzas |
| Header surgery | 0005–0009 | new fields on public structs (genksyms-hidden) |
| NFS plumbing | 0010–0011 | mount-option parsing, super.c hooks |
| NFS3 EXTEND op | 0012 | non-standard NFSv3 op (`NFS3PROC_EXTEND`) |
| RPC multipath hooks | 0013–0014 | clnt.c and xprt.c dispatch hooks |
| Exports | 0015–0018 | include guard + four export patches |
| Client + remount | 0019–0020 | propagate the option, restore an OE API |

Patch numbers are stable — `0007` is `0007` across `ubuntu-6.8/`,
`ubuntu-6.14/`, and `ubuntu-7.0/`. The *content* drifts with the
target kernel; the sidebar at the end (§8.8) covers the deltas.

## 8.1 Build wiring (0001–0004)

The first four patches teach the kernel build system that two new
things exist (`enfs_adapter.o` belongs in `nfs.ko`,
`sunrpc_enfs_adapter.o` belongs in `sunrpc.ko`) and add the matching
Kconfig symbols. None of them change runtime behaviour — they just
let the rest of the series compile.

### Patch 0001 — `fs/nfs/Makefile`: build enfs

Two trailing lines: `nfs-y += enfs_adapter.o` (under `ifneq
($(CONFIG_ENFS),)`) and `obj-$(CONFIG_ENFS) += enfs/`. The first
links the adapter (the registry `enfs.ko` plugs into) into
`nfs.ko`; the second descends into `fs/nfs/enfs/` to build
`enfs.ko`. Verbatim port from `vendor/openeuler/fs/nfs/Makefile`.
Long form:
[`docs/changes/001-fs-nfs-Makefile-build-enfs.md`](../changes/001-fs-nfs-Makefile-build-enfs.md).

### Patch 0002 — `net/sunrpc/Makefile`: build sunrpc_enfs_adapter

One line: `sunrpc-$(CONFIG_SUNRPC_ENFS) += sunrpc_enfs_adapter.o`.
Compiles the adapter (which provides
`rpc_multipath_ops_register/unregister/get/put` plus thin wrappers
called from `clnt.c` / `xprt.c` after patches 0013/0014) into
`sunrpc.ko`. The registry must live in `sunrpc.ko` so the per-RPC
dispatch path can call it inline. Long form:
[`docs/changes/002-...md`](../changes/002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.md).

### Patch 0003 — `fs/nfs/Kconfig`: `CONFIG_ENFS`

Adds `config ENFS` (tristate, depends on `NFS_FS` and `X86 || X86_64
|| ARM64`, `select`s `SUNRPC_ENFS`) plus `config ENFS_KUNIT_TEST`
(preserved verbatim from OE; tests not currently built). The
`select` arrow auto-enables the SunRPC half. Long form:
[`docs/changes/003-...md`](../changes/003-fs-nfs-Kconfig-add-CONFIG_ENFS.md).

### Patch 0004 — `net/sunrpc/Kconfig`: `CONFIG_SUNRPC_ENFS`

The SunRPC counterpart. `bool` (not tristate) because the conditional
code lives inside `sunrpc.ko`. Gated by patches 0002, 0005, and
0006. Long form:
[`docs/changes/004-...md`](../changes/004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.md).

## 8.2 Header surgery (0005–0009)

The five header patches add per-client / per-server / per-task fields
that enfs needs to attach its state to objects owned by the stock
NFS / SunRPC code. All five use the `__GENKSYMS__` trick described in
[chapter 7 §7.4](./07-locking-and-concurrency.md#74-the-__genksyms__--crc-trick)
so that the module CRC tables remain compatible with any unmodified
consumer of the headers.

### Patch 0005 — `include/linux/sunrpc/clnt.h`: `cl_enfs` + `multipath_option`

Adds three fields to `clnt.h`, all hidden from genksyms:

- `struct rpc_clnt::cl_enfs : 1` — set when this client has
  multipath enabled. The hot-path discriminator (chapter 5).
- `struct rpc_clnt::multipath_option` (`void *`) — opaque per-client
  multipath state. enfs casts it to its own
  `struct multipath_client_info`.
- `struct rpc_create_args::multipath_option` (`void *`) — the same
  pointer, threaded through `rpc_create()` so the SunRPC adapter can
  copy it onto the freshly-allocated `rpc_clnt` (see also patch
  0019).

The bitfield split is the most ABI-load-bearing part. Genksyms sees
the bitfield word with `cl_shutdown` as the last bit; gcc sees it
with `cl_enfs` appended. CRC of every `EXPORT_SYMBOL` whose signature
mentions `struct rpc_clnt *` is unchanged. Long form:
[`docs/changes/005-...md`](../changes/005-include-sunrpc-clnt.h-add-multipath-fields.md).
The `__GENKSYMS__` rationale also lives in chapter 7.

### Patch 0006 — `include/linux/sunrpc/sched.h`: `RPC_TASK_ENFS` + `RPC_TASK_FIXED`

Defines two new `tk_flags` bits:

- `RPC_TASK_ENFS = 0x0008` — marks an RPC task that originated
  inside enfs (so accounting goes to the enfs side).
- `RPC_TASK_FIXED = 0x0020` — pins a task to its current xprt;
  failover must not reroute it. Used by pm_ping.

This patch is **flagged as the highest-risk in the series.** OE 6.6
allocated `RPC_TASK_FIXED = 0x0040`, but Ubuntu 7.0 added
`RPC_TASK_NETUNREACH_FATAL = 0x0040` post-6.6 — a direct collision.
We rebased to `0x0020`, which the long-form changelog notes may
*itself* alias `RPC_CALL_MAJORSEEN` in the same flags word. The
verification step (read the actual 7.0 `sched.h` and confirm a
genuinely-free bit) is a TODO in
[`docs/changes/006-...md`](../changes/006-include-sunrpc-sched.h-add-RPC_TASK_ENFS.md).
Until that is resolved, the documented failure mode is "noisy
diagnostics, no data corruption" — both NFS and enfs are RPC-idempotent
for the operations involved, and the only risk is mis-categorised
log lines or pings that get unexpectedly rerouted.

### Patch 0007 — `include/linux/nfs_fs_sb.h`: enfs fields

Three additions, all under `#if !defined(__GENKSYMS__) &&
IS_ENABLED(CONFIG_ENFS)`: `struct nfs_client::cl_multipath_data`
(`void *`, slot for per-`nfs_client` enfs state),
`struct nfs_server::enfs_flags` (`int`, bitfield for
`ENFS_SERVER_FLAG_*`), and two `static inline` refcount helpers
`nfsclient_refinc` / `nfsclient_refdec`. The genksyms guard is
essential because `nfs.ko`'s exports mention these structs — every
consumer (`nfsv3.ko`, `nfsv4.ko`, `nfsd.ko`) would fail to load
otherwise. Long form:
[`docs/changes/007-...md`](../changes/007-include-nfs_fs_sb-add-enfs-fields.md).

### Patch 0008 — `include/linux/nfs_xdr.h`: `nfs_extend_xdr_arg`

Adds a small XDR carrier struct (`maxsize`, `buflen`, `pBuf`) to a
public header so the encoder/decoder pair (patch 0012) in `nfs.ko`
and the caller in `enfs.ko` (`exten_call.c`) see the same layout.
Wrapped in `#if IS_ENABLED(CONFIG_ENFS)`. No exported symbol
mentions the struct, so genksyms is unaffected. Long form:
[`docs/changes/008-...md`](../changes/008-include-nfs_xdr-add-extend-xdr-arg.md).

### Patch 0009 — `fs/nfs/internal.h`: `enfs_option` slots

Adds two `void *enfs_option` slots, one on
`struct nfs_client_initdata` and one on `struct nfs_fs_context`.
The middle stops in the propagation chain (chapter 3) that carries a
parsed `enfs_info=` from the mount-option parser to
`rpc_create_args::multipath_option` (slot from 0005). Patch 0019
closes the loop. `internal.h` is private to `fs/nfs/` so genksyms is
unaffected. Long form:
[`docs/changes/009-...md`](../changes/009-fs-nfs-internal-add-enfs-option-fields.md).

## 8.3 NFS plumbing (0010–0011)

These two patches teach `fs/nfs/` to actually *parse* the new mount
options, validate them, kick off the capability probe, and wire the
remount path. They are the largest two patches in the series by hunk
count.

### Patch 0010 — `fs/nfs/super.c`: include + remount + mount-complete hooks

Three hook sites:

1. `#include "enfs_adapter.h"` so this TU can call the adapter
   functions.
2. In `nfs_reconfigure()` (the remount path), if `ctx->enfs_option`
   is non-NULL, call `nfs_remount_iplist(...)` to push the new path
   list into the live multipath state. On failure, release the
   parsed options and return the error.
3. In `nfs_get_tree_common()` (the mount-completion path), kick
   `enfs_trigger_get_server_capability(server)` immediately after the
   superblock is marked active. This is the fire-and-forget probe
   that asks each remote endpoint "do you speak the EXTEND op?"

All three are guarded by `CONFIG_ENFS`. Verbatim port from OE — no
deviations. Long form:
[`docs/changes/010-...md`](../changes/010-fs-nfs-super-add-enfs-hooks.md).

### Patch 0011 — `fs/nfs/fs_context.c`: `enfs_info=` mount option

The single largest patch in the series — seven hook sites that teach
the NFS fs_context parser to accept five new tokens:

- `remoteaddrs=` — the list of server-side IPs (tilde-separated).
- `localaddrs=` — the list of client-side source IPs.
- `enfs_info=` — historically a JSON-ish blob; currently accepted
  but not acted on at parse time (consumed later by enfs).
- `slookupcache=`, `alookupcache=` — historical lookup-cache mode
  hints; same situation.

The handlers split: `remoteaddrs=` / `localaddrs=` route through
`enfs_parse_mount_options()` (which lives in `enfs_adapter.c`,
registered by `enfs.ko`) and translate `-ENOMEM` / `-ENOSPC` /
`-EINVAL` into specific error labels. The other three are accepted
but produce no parse-time work.

The patch also adds two new error labels (`out_limit`, `out_nomem`)
to give the user different messages for "too many addrs" vs "out of
memory", and lifecycle hooks at fs_context construction
(`ctx->enfs_option = NULL`) and destruction
(`enfs_free_mount_options(ctx)`). Long form:
[`docs/changes/011-...md`](../changes/011-fs-nfs-fs_context-add-enfs_info-mount-option.md).

## 8.4 NFS3 EXTEND op (0012)

### Patch 0012 — `fs/nfs/nfs3xdr.c`: `NFS3PROC_EXTEND`

End-to-end XDR support for the OpenEuler-only NFSv3 op
`NFS3PROC_EXTEND = 22`. Five additions, all under `CONFIG_ENFS`:

1. Includes / defines: pull `struct nfs_extend_xdr_arg` from patch
   0008's header; define `EXTEND_CMD_MAX_BUF_LEN = 800 KiB`; fall
   back to a local `#define NFS3PROC_EXTEND 22` if the UAPI header
   doesn't define it (which it doesn't on stock Ubuntu 7.0; we avoid
   patching UAPI headers as policy — see [chapter 9
   §9.4](./09-compat-shims.md#94-nfs3proc_extend)).
2. Two argsize macros (`NFS3_extendargs_sz`, `NFS3_extendres_sz`).
3. Encoder `nfs3_xdr_enc_extend3args()` — writes the opaque payload
   as a length-prefixed byte array.
4. Decoder `nfs3_xdr_dec_extend3res()` — reads the NFS3 status, then
   a length-prefixed opaque payload, bounds-checked against
   `decArg->maxsize`. Returns `-E2BIG` on overrun (matches OE's
   choice).
5. `PROC(EXTEND, extend, extend, 0)` slot in `nfs3_procedures[]` at
   index 22.

Standard NFSv3 servers (anything not OE-derived) reply with
`NFS3ERR_NOTSUPP` to op 22; enfs treats that response as
"multipath-incapable" and skips the server from path-state polling.
The encoder/decoder pair is reachable from `enfs.ko` only via patch
0018's export of `nfs3_procedures`. Long form:
[`docs/changes/012-...md`](../changes/012-fs-nfs-nfs3xdr-extend-call.md).

## 8.5 RPC multipath hooks (0013–0014)

These two patches plant the actual *call sites* of the
`rpc_multipath_ops_*` adapter inside the SunRPC dispatch path.
Without them, the registry that 0002 builds and the fields that 0005
adds are inert.

### Patch 0013 — `net/sunrpc/clnt.c`: 8 hook sites

Eight sites in the RPC client core (full table:
[`docs/changes/013-...md`](../changes/013-net-sunrpc-clnt-multipath-hooks.md)).
Highlights:

- `rpc_multipath_ops_create_clnt(args, clnt)` and
  `rpc_multipath_ops_releas_clnt(clnt)` (yes, the typo "releas" is
  preserved from OE — renaming would touch three files) bracket the
  client lifecycle.
- `rpc_multipath_ops_inc_queuelen(xprt)` /
  `rpc_multipath_ops_dec_queuelen(xprt)` keep enfs's per-xprt queue
  counter in sync with SunRPC's.
- `rpc_multipath_ops_set_transport(task, clnt)` lets enfs override
  stock xprt selection for tasks marked `RPC_TASK_ENFS`.
- `RPC_MULTIPAHT_UPDATE_RPC_PROC(task, p, clnt)` (macro) replaces the
  fixed `cl_prog` / `cl_vers` writes in the call-header encoder.
  Inlines to zero overhead when `multipath_option` is NULL.
- `rpc_multipath_switch_set_roundrobin(clnt, xps)` falls through to
  the stock helper when no multipath is registered.

**Two hooks deferred.** OE's 6.6 tree has additional
`case -ETIMEDOUT` failover hooks in `call_status` and `call_refresh`
that depend on a local `failover` boolean not present in 7.0. Site
mapping is invasive; deferred. Net effect: failover-on-timeout for
stuck transports is partially degraded — load-balancing and
initial-mount work, but a permanently-down transport is not retired
by these hooks. pm_ping (chapter 4) catches the case eventually, so
the impact is delayed rather than absent.

### Patch 0014 — `net/sunrpc/xprt.c`: 7 hook sites

Seven sites in the xprt layer (full table:
[`docs/changes/014-...md`](../changes/014-net-sunrpc-xprt-multipath-hooks.md)).
Notable:

- Three `out_sleep:` paths call
  `rpc_multipath_ops_adjust_task_timeout(task, NULL)` *before* the
  sleep so enfs can recompute the per-path timeout against the new
  transport's RTT — without this, slow paths inherit fast paths'
  tight timeouts.
- `xprt_alloc_slot` calls `rpc_multipath_ops_init_task_req(task,
  req)` to stamp the rqst with multipath bookkeeping in the only
  safe slot before the rqst is visible to other CPUs.
- `xprt_create_transport` replaces the stock `kstrdup(...,
  servername)` with `rpc_multipath_set_servername(...)` — enfs needs
  to reserve extra leading bytes for an `"<idx>:<name>"` prefix to
  tell siblings apart. Also calls `rpc_multipath_ops_create_xprt`
  and bails on ENOMEM. `xprt_destroy` mirrors with
  `rpc_multipath_free_servername(xprt)`.

All seven ported cleanly — no deferred hooks. The only externally
observable effect is the synthetic servername (`0:server`,
`1:server`, ...) in `/sys/kernel/debug/sunrpc/rpc_xprt/...`.

## 8.6 Header guard + exports (0015–0018)

These four patches don't add functionality; they make existing
functionality reachable across the module boundary.

### Patch 0015 — `fs/nfs/internal.h`: include guard

Wraps `fs/nfs/internal.h` in `#ifndef _ENFS_NFS_INTERNAL_H_GUARD_`.
Stock has no guard — fine when each TU includes the header exactly
once. Patch 0010 (and 0011, 0019) make `super.c` /
`fs_context.c` / `client.c` `#include "enfs_adapter.h"` *and*
`#include "internal.h"`, and `enfs_adapter.h` transitively includes
`internal.h`. Without a guard, the second include fires ~30
`redefinition` errors. The guard macro is enfs-prefixed
(`_ENFS_NFS_INTERNAL_H_GUARD_`) so it doesn't clash with any
hypothetical mainline-added guard. Long form:
[`docs/changes/015-...md`](../changes/015-fs-nfs-internal-add-include-guard.md).

### Patch 0016 — `net/sunrpc/clnt.c`: export `rpc_task_get_next_xprt`

Drops `static` from `rpc_task_get_next_xprt()` and adds
`EXPORT_SYMBOL_GPL`. Function body unchanged. enfs's failover state
machine calls it from three sites in `failover_path.c` to advance a
stuck task to the next transport in the round-robin. Without the
export, `enfs.ko` fails to load with `Unknown symbol`. Long form:
[`docs/changes/016-...md`](../changes/016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.md).

### Patch 0017 — `net/sunrpc/xprtmultipath.c`: 5 helper exports {#patch-0017}

Five helpers in `xprtmultipath.c` made callable across the module
boundary:

| Helper | Stock state | Action |
|---|---|---|
| `xprt_switch_add_xprt_locked` | `static` | drop `static`, add `EXPORT_SYMBOL_GPL` |
| `rpc_xprt_switch_remove_xprt` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `xprt_switch_get` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `xprt_switch_put` | non-static, no export | add `EXPORT_SYMBOL_GPL` |
| `xprt_iter_get_next` | non-static, no export | add `EXPORT_SYMBOL_GPL` |

This is the entire xprt-switch API surface enfs needs, bundled into
one patch. The locked-vs-public distinction matters: enfs's adapter
sometimes already holds `xps_lock` and must call the locked variant
directly to avoid deadlock — see [chapter 7
§7.7](./07-locking-and-concurrency.md#77-locked-vs-lockless-helpers).
Long form:
[`docs/changes/017-...md`](../changes/017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.md).

### Patch 0018 — `sunrpc/nfs`: export `xprt_release` + `nfs3_procedures`

Two unrelated `EXPORT_SYMBOL_GPL` lines in two files:

- `xprt_release` in `net/sunrpc/xprt.c` — enfs's failover code
  needs it to drop the per-task xprt reference before reassigning
  the task to a new xprt (otherwise the ref leaks).
- `nfs3_procedures` in `fs/nfs/nfs3xdr.c` — the procedure table
  enfs's EXTEND-op dispatch indexes into directly
  (`exten_call.c:609, 646`). Without the export, enfs would have to
  reimplement the procedure-info struct locally with obvious drift
  risk.

Both targets are non-static in stock 7.0 but un-exported. The
`nfs3_procedures` export is on the *stock* version of the array (22
entries); patch 0012 adds the 23rd `PROC(EXTEND, ...)` slot
separately, before this export point. The two patches are
independent — 0018 would still apply (and the export still useful)
if 0012 were dropped. Long form:
[`docs/changes/018-...md`](../changes/018-sunrpc-nfs-export-helpers-for-enfs.md).

## 8.7 Client + remount (0019–0020)

The last two patches close two distinct gaps: 0019 propagates the
mount-time `enfs_option` end-to-end, and 0020 re-introduces an OE
API that mainline removed.

### Patch 0019 — `fs/nfs/client.c`: propagate `enfs_option`

Without this patch, every other patch in the series compiles and
links — but `enfs_option` parsed by 0011 never reaches
`rpc_create()` in a non-NULL state. Mounts succeed silently with
only the primary server receiving traffic. Verifiable failure mode:
`/sys/kernel/sunrpc/xprt-switches/switch-X/xprt_switch_info` shows
`num_xprts: 1` instead of the expected N.

Three hook sites where OE had two (the third is a deviation
explained below):

1. `nfs_init_server()` — copy `ctx->enfs_option` into
   `cl_init.enfs_option`.
2. `nfs_create_rpc_client()` — forward `cl_init->enfs_option` into
   `rpc_create_args::multipath_option` (the slot from patch 0005).
3. `nfs_init_client()` — call `nfs_create_multi_path_client(clp,
   cl_init)` *before* `nfs_create_rpc_client()` so the per-client
   multipath state (`cl_multipath_data`, slot from patch 0007) is
   already allocated when the `create_clnt` adapter callback fires.

**Why three hooks and not two.** OE bundled the `nfs_create_multi_path_client`
call into the same site as `nfs_create_rpc_client`'s extension.
Ubuntu 7.0's `nfs_init_client()` has a `cl_cons_state == NFS_CS_READY`
fast-exit return that moved relative to OE 6.6. Putting the
multi_path_client create *before* that fast-exit would re-allocate
state for clients reused by a second mount — a leak. Putting it
*after* the fast-exit is correct but means it's its own hook site.
Long form:
[`docs/changes/019-...md`](../changes/019-fs-nfs-client-propagate-enfs-option.md).

### Patch 0020 — `net/sunrpc/clnt.c`: re-add `rpc_clnt_test_xprt`

Re-introduces the older one-shot `rpc_clnt_test_xprt(clnt, xprt,
ops, data, flags)` API that mainline dropped in favour of
`rpc_clnt_setup_test_and_add_xprt()`. enfs's pm_ping subsystem still
calls the older API directly to send NULL-RPC liveness probes:

```c
/* fs/nfs/enfs/pm_ping.c:321 */
rpc_clnt_test_xprt(work_info->clnt, work_info->xprt,
                   &pm_ping_set_status_ops, work_info, RPC_TASK_FIXED);
```

Note the `RPC_TASK_FIXED` argument — a ping must stay on its target
xprt (otherwise it stops measuring "is *this transport* alive"). The
function body is OE's verbatim, wrapped in `#if
IS_ENABLED(CONFIG_SUNRPC_ENFS)` so a non-enfs build is byte-for-byte
the same as stock.

The patch header documents an earlier-attempted compat shim that
returned `0` unconditionally. That broke catastrophically — the
caller treated `0` as "no task queued, free the work_info now",
while the actual queued task ran later with the now-freed pointer.
Use-after-free. The current implementation queues a real RPC and
returns `1` on success (matching OE's convention), `PTR_ERR(task)`
on failure. The corresponding declaration lives in
[`compat/enfs_compat.h`](../../compat/enfs_compat.h) (chapter 9).
Long form:
[`docs/changes/020-...md`](../changes/020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.md).

## 8.8 Sidebar: per-target rebase variants

The same 20 numbered patches exist under
`patches/ubuntu-6.8/`, `patches/ubuntu-6.14/`, and
`patches/ubuntu-7.0/`. Their *intent* is identical; the *content*
drifts because the surrounding stock code does. Three concrete
examples:

**Patch 0005 (`clnt.h` field additions).** The bitfield split
anchors against different surrounding fields per target:

- 6.8: `cl_shutdown` is the *last* bitfield in the word (terminated
  with `;`). `cl_netunreach_fatal` does not exist yet upstream. The
  patch's hunk anchors on `cl_shutdown : 1;`.
- 6.14: same bitfield situation as 6.8 (`cl_shutdown` last,
  no `cl_netunreach_fatal`), but the struct's *trailing* fields
  changed — kernel 6.14 added `atomic_t cl_task_count;` after
  `pipefs_sb`. `multipath_option` now anchors after `cl_task_count`.
- 7.0: `cl_netunreach_fatal` was added between `cl_shutdown` and the
  end of the word, and the bitfield is now `cl_shutdown : 1,` (with
  comma, not semicolon — the word continues). The patch's hunk shape
  changes to insert `cl_enfs : 1,` between `cl_shutdown` and
  `cl_netunreach_fatal`.

The semantic effect is identical in all three. The patch text
differs because diff3-style anchors must match the actual surrounding
lines.

**Patch 0017 (xprtmultipath helper exports).** 6.8 still has
`xprt_iter_get_xprt` natively. The 6.8 patch exports *six* helpers,
including that one. 6.14 and 7.0 dropped `xprt_iter_get_xprt`
upstream in favour of `xprt_iter_get_next` (which advances rather
than peeks); the 6.14 / 7.0 patches export only five helpers.
[Chapter 9 §9.10](./09-compat-shims.md#910-xprt_iter_get_xprt) covers
the corresponding compat shim.

**Patch 0019 (client.c three-hook split).** As described above, the
third hook is a 7.0-specific deviation forced by the
`NFS_CS_READY` fast-exit position. The 6.8 and 6.14 variants still
have three hooks for the same reason — the upstream relocation that
forced our hand happened well before 7.0 — but the line numbers and
hunk anchors differ.

**Branch hygiene.** The three target directories are kept in step by
hand: when a new patch lands, it is written first against whichever
target the developer is iterating on (usually 7.0 because that's the
GA target), then rebased into the other two. The commit message of
the variants typically reads "Same as the 7.0 patch, but rebased
for kernel X.Y where Z." — see, e.g., the 6.8 variant of patch 0005,
which carries that exact phrasing. CI builds all three targets
(chapter 10), so a divergent patch fails fast.
