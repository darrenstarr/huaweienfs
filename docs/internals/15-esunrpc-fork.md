# 15. The esunrpc fork — design, scope, first build

> Prerequisites: [chapter 8](./08-patch-series.md) (the legacy patch
> stack we're starting to replace) and [chapter 9](./09-compat-shims.md)
> (the compat header that's now conditionally inert for esunrpc TUs).
> This chapter introduces a parallel, additive kernel module
> (`esunrpc.ko`) that is the first step on a multi-PR roadmap to
> retire the legacy "patch + replace stock kernel modules" approach.

The legacy stack ships replacement copies of `sunrpc.ko`, `nfs.ko`,
`nfsv3.ko`, `nfsv4.ko`, `lockd.ko`, `nfs_acl.ko` for the host kernel,
each carrying minor patches against stock. It works, but it is
structurally non-upstreamable: no distribution will accept a package
that overrides core kernel modules. To get the project to a state
where it can plausibly land in debian (or any other vendor's archive),
we need an **additive** stack — modules that coexist with the host's
stock NFS instead of replacing them.

`esunrpc.ko` is PR 1 of that road. It is a forked, renamed copy of
the kernel's sunrpc client subset. Every exported symbol is renamed
so it does not collide with `sunrpc.ko`; every global-namespace
resource (proc dir, debugfs dir, sysfs kset, filesystem name,
workqueue, slab cache) is renamed for the same reason. Both modules
load simultaneously without a single warning.

PR 1 only proves the module loads. No client work, no NFS layer,
no actual RPC traffic. PRs 2-7 build the rest of the clean stack on
top. The roadmap is in §15.8.

## 15.1 Why fork

Three reasons:

1. **Distro shippability.** Stock sunrpc is part of the kernel
   tree. The current package replaces it. No distro accepts that.
   A forked, renamed module just adds itself to `lib/modules/.../updates/`
   and leaves stock alone.
2. **CRC dependency hell.** The legacy stack has to keep symbol CRCs
   matching stock so other unmodified modules (lockd, nfs_acl,
   nfsv4) can link against the patched sunrpc. That's why
   `compat/enfs_compat.h` had to ship the `__GENKSYMS__`-hidden
   field trick. With a fully separate `esunrpc.ko`, there's nothing
   to align CRCs against — stock continues to work, esunrpc has its
   own ABI.
3. **Multipath as a first-class API.** The legacy stack added
   multipath via a `multipath_ops` indirection inside sunrpc that
   `enfs.ko` registers callbacks into. Useful as a hack, ugly as an
   API. A clean fork lets us redesign multipath as a first-class
   esunrpc primitive (see PR 5 in §15.8).

## 15.2 What we forked from

- Pin: `linux-7.0.0-15.15` (Ubuntu 26.04 LTS general availability)
- Captured in `vendor/esunrpc/UPSTREAM-REVISION`
- Files copied: see `vendor/esunrpc/MANIFEST` for the full list
- Reproducible: `bash scripts/fork-sunrpc.sh <upstream_kernel_src>`
  re-applies the trim + rename rules; idempotent

## 15.3 What we kept and what we dropped

The fork is client-only by intent — esunrpc is for talking *to* an
NFS server, not running one.

| Kept (.c) | Why |
|---|---|
| `clnt.c`, `xprt.c`, `xprtsock.c`, `sched.c`, `xdr.c` | core RPC client machinery |
| `xprtmultipath.c` | foundation for first-class multipath in PR 5 |
| `auth.c`, `auth_unix.c`, `auth_null.c`, `auth_tls.c` | client-side auth flavours used by NFSv3 |
| `addr.c`, `rpcb_clnt.c`, `timer.c`, `socklib.c` | address parsing, portmapper, RTT, socklib |
| `rpc_pipe.c` | `/sys/kernel/esunrpc/` plumbing for callouts |
| `cache.c` | auth credential cache machinery |
| `stats.c` | per-net `/proc/net/esunrpc/` tree |
| `sysfs.c`, `sysctl.c`, `debugfs.c` | observability, kept on day 1 because clnt.c references them at module init |
| `svcauth_unix.c` | dual-use: clnt.c needs `ip_map_cache_*`/`unix_gid_cache_*` from this file at module init, even though most of svcauth_unix is server-side |
| `backchannel_rqst.c` | `xprt.c` references its symbols at link time even though backchannel is NFSv4-only |
| `sunrpc_syms.c` → `esunrpc_syms.c` | module init/exit (server-init calls stripped by post-rename surgery) |

| Dropped (.c) | Why |
|---|---|
| `svc.c`, `svc_xprt.c`, `svcsock.c`, `svcauth.c` | server-side RPC; not part of a client fork |
| `auth_gss/*` | GSS/krb5 auth; v3 uses AUTH_SYS, defer |
| `xprtrdma/*` | NFS-over-RDMA; would significantly expand scope |

| Stub provided | Why |
|---|---|
| `svc_print_xprts()` (no-op) | `sysctl.c` calls it for `/proc` rendering of registered server transports — esunrpc has none, returns 0 |

## 15.4 Rename rules

The fork script (`scripts/fork-sunrpc.sh`) applies these
transformations in this order. Order matters — earlier rules can
match patterns that later rules would rewrite.

| Step | Rule | Why |
|---|---|---|
| 1 | `<linux/sunrpc/X.h>` → `<esunrpc/X.h>` | so esunrpc TUs include our forked headers, not stock |
| 1b | `_SUNRPC_*_H_` → `_ESUNRPC_*_H_` (header guards) | otherwise stock and forked headers share the same guard, and whichever is included second is silently skipped |
| 2 | internal `"sunrpc.h"` → `"esunrpc.h"` (file rename + include rewrite) | the internal helpers header collides as well |
| 3 | `sunrpc_syms.c` → `esunrpc_syms.c` (file rename) | so the module-init source is named after the module |
| 4 | every `EXPORT_SYMBOL[_GPL]`'d identifier `X` → `esunrpc_X` (and every reference to `X` in the forked tree) | the load-bearing rename: this is what makes esunrpc.ko's symbol table disjoint from stock sunrpc.ko's |
| 4b | strip `<trace/events/sunrpc.h>` includes; emit a stub header that defines all `trace_*` macros as no-ops | the kernel tracepoint headers transitively include stock `<linux/sunrpc/svc.h>`, which redefines our struct types |
| 4d | emit `esunrpc_server_stubs.c` with `svc_print_xprts()` no-op | `sysctl.c` references this, server-only files dropped |
| 4e | rename **global resource names** (proc dir, fs name, slab caches, workqueue, debugfs dirs, sysfs kset) to `esunrpc*` | otherwise modprobe fails with `Device or resource busy` / `kobject already registered` because stock owns those names |
| 5 | post-surgery on `esunrpc_syms.c`: bump `MODULE_DESCRIPTION`; strip server-only init calls (`svc_init_xprt_sock`, `svc_cleanup_xprt_sock`, `auth_domain_cleanup`) | preserves the load-time behaviour while not depending on dropped server files |

The rename also takes care of `compat/enfs_compat.h`. That header is
force-included into every TU by the top-level Kbuild (legacy
behaviour). For esunrpc TUs the Kbuild adds `-DBUILDING_ESUNRPC=1`,
which gates the entire compat header into a no-op — necessary
because the compat header includes stock `<linux/sunrpc/clnt.h>`,
which would re-introduce stock struct definitions and conflict with
our forked headers.

## 15.5 Reproducing — the fork script

`scripts/fork-sunrpc.sh` is the source of truth. To regenerate
`vendor/esunrpc/` against a fresh upstream pin:

```bash
# 1. Make sure you have an extracted upstream source tree.
# This file uses the cache that scripts/fetch-vendor-ubuntu.sh
# populates, but any extracted linux-N.N.N tarball works.
KSRC=~/.cache/enfs-vendor/linux_7.0.0-15.15/linux-7.0.0

# 2. Regenerate the fork.
bash scripts/fork-sunrpc.sh "$KSRC"

# 3. Review the diff like any other update.
git diff vendor/esunrpc/
```

The script is idempotent: re-running with the same input replaces
the tree with byte-identical content. Diff between runs against a
new upstream pin is the meaningful review surface for kernel-bump
PRs.

## 15.6 Smoke test for "module loads"

After `make port + make modules` builds `esunrpc.ko`:

```bash
# install + load
sudo cp src/net/esunrpc/esunrpc.ko /lib/modules/$(uname -r)/updates/
sudo depmod -a
sudo modprobe esunrpc

# expected results
lsmod | grep '^esunrpc'
# esunrpc               401408  0

ls /proc/net/esunrpc
# auth.unix.gid  auth.unix.ip

ls /sys/kernel/esunrpc
# rpc-clients  xprt-switches

sudo grep ' [tTrR] esunrpc_' /proc/kallsyms | wc -l
# ~244

sudo dmesg | tail -5
# RPC: Registered named UNIX socket transport module.
# RPC: Registered udp transport module.
# RPC: Registered tcp transport module.
# RPC: Registered tcp-with-tls transport module.
# RPC: Registered tcp NFSv4.1 backchannel transport module.

# unload
sudo rmmod esunrpc
# (no errors; dmesg shows "Unregistered ..." messages)
```

Lab-confirmed on `<LAB_HOST_04>` (Ubuntu 26.04, kernel 7.0.0-15)
on 2026-05-05. Stock `sunrpc.ko` was loaded and active throughout
(supporting the host's existing NFS mounts) — esunrpc loaded
alongside it without a single warning.

## 15.7 What this PR is NOT

**Not** a working RPC client. esunrpc.ko exposes its renamed
symbols, has its own per-net `/proc` tree, owns its own slab
caches and workqueue — but no caller is connected to it yet. PR 2
(below) is the first end-to-end path: a tiny test driver that
calls `esunrpc_rpc_create` + `esunrpc_rpc_call_null` against
`/usr/sbin/rpcinfo -t localhost`-style targets to prove the
plumbing actually moves bytes.

**Not** symbol-isolated from stock sunrpc. `lsmod` shows esunrpc
listed under sunrpc's "used by" line — the fork still references
some non-exported sunrpc helpers that resolve to stock at load
time. Closing that gap (renaming non-static internal helpers,
not just exports) is part of PR 5's "first-class API" work and
is intentionally out of scope here.

**Not** a multipath layer. esunrpc inherits `xprtmultipath.c`
verbatim from upstream sunrpc. Making multipath first-class (i.e.,
collapsing the legacy `multipath_ops` indirection) is PR 5.

**Not** a replacement for the legacy stack. Both stacks coexist
in the DKMS package. Cutover happens at PR 7, after the new stack
reaches feature parity. Customers running v0.1.x are unaffected
during the transition.

## 15.8 Roadmap to PR 7

| PR | Title | Scope | Status |
|---|---|---|---|
| 1 | esunrpc.ko skeleton | this PR — fork + rename + load | filed |
| 2 | First esunrpc client end-to-end | NULL RPC over TCP through `esunrpc_rpc_create` + a small test module | open |
| 3 | enfs_nfsv3.ko skeleton | forked NFS layer, trimmed to v3-only, mount + GETATTR works against a real server | open |
| 4 | Data path | READ / WRITE / direct I/O / pagelist / fsync semantics | open |
| 5 | First-class multipath in esunrpc | drop the legacy `multipath_ops` indirection; rename non-exported internal symbols too; make `enfs.ko` link against esunrpc only | open |
| 6 | Feature parity audit | NLM (locking), ACL, mount options, statfs, oddments | open |
| 7 | Retire legacy patch stack | remove the 23 stock-kernel patches, drop the DKMS-replacement modules, ship clean | open |

The legacy patched stack stays buildable on the `legacy` branch
throughout. New work lands on `feat/esunrpc-*` branches off `main`.
The cutover at PR 7 is the only point where a customer running
v0.1.x has to decide between staying on the legacy package and
upgrading to the clean stack.

## 15.9 Risks and known issues

| Risk | Status | Mitigation |
|---|---|---|
| esunrpc still depends on stock sunrpc at load time | known | PR 5 will rename non-static internal helpers, eliminating the link-time dependency |
| sed-based rename misses unusual occurrences | low risk in PR 1 | the rename is mechanical; build errors guide cleanup; the `.rename-rules.sed` file is checked into `vendor/esunrpc/` for audit |
| Future kernel bump (7.2, 8.0) introduces new global names that collide | inevitable | the pattern is "modprobe fails → dmesg → add a sed line to fork-sunrpc.sh §4e → re-fork"; the script accumulates the rename rules over time |
| Stripping tracepoints loses observability | acceptable for PR 1 | PR 5+ can re-add esunrpc-specific tracepoints in a clean namespace |

## 15.10 What's in the diff

```
Kbuild                                        (~30 lines added)
dkms.conf.in                                  (~15 lines added)
Makefile                                      (~3 lines added)
scripts/build-src-tree.sh                     (~15 lines added)
scripts/fork-sunrpc.sh                        new (~140 lines)
compat/enfs_compat.h                          (~10 lines wrapping)
vendor/esunrpc/MANIFEST                       new
vendor/esunrpc/UPSTREAM-REVISION              new
vendor/esunrpc/.rename-rules.sed              new (generated)
vendor/esunrpc/net/esunrpc/*.c                ~24 files (forked + renamed)
vendor/esunrpc/net/esunrpc/*.h                ~6 files
vendor/esunrpc/include/esunrpc/*.h            ~22 public headers
docs/internals/15-esunrpc-fork.md             new (this chapter)
docs/internals/README.md                      TOC entry
```

Of those, the bulk (~26K lines under `vendor/esunrpc/`) is the
mechanically renamed upstream source. The hand-written changes
add up to ~250 lines.
