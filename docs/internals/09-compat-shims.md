# 9. Compat shims

> Prerequisites: [chapter 8](./08-patch-series.md) for cross-references
> to the patches that the shims complement, and a basic understanding
> of `LINUX_VERSION_CODE` / `KERNEL_VERSION` macros from kernel
> headers.

The compat layer is exactly one file — [`compat/enfs_compat.h`](../../compat/enfs_compat.h)
— and a single line in [`Kbuild`](../../Kbuild) that arranges for
that file to be force-included into every translation unit. This
chapter walks through every shim in the header, identifies what
kernel-version drift it papers over, and classifies each shim as
compatibility, graceful-degradation, or TODO.

## 9.1 The model: force-include

```make
# Kbuild line 30
ccflags-y += -include $(src)/compat/enfs_compat.h
```

`gcc -include` prepends the named header to every TU before any
`#include` directive in the source. The effect is that every `.c`
file enfs builds — patched stock kernel sources, OE-only enfs
sources, the adapter glue — effectively starts with the contents of
`enfs_compat.h`.

There is no opt-out. Any symbol defined in the compat header is
visible everywhere. Any `static inline` or `#define` there overrides
or adds to whatever the kernel headers provide. This is deliberate:
compat shims should not require coordinated `#include` lines in
vendored sources, because adding such an include would mean editing
the vendored source directly — which is exactly the modification
mode we adopted Option B′ to avoid.

A second consequence: the compat header sees only what the
preprocessor sees *first*. It includes `<linux/version.h>` to gate on
`LINUX_VERSION_CODE`, but cannot rely on, say,
`<linux/sunrpc/clnt.h>` having been included before it (that header
might be included only by some TUs). Where a shim needs a struct
type, it forward-declares (e.g. `struct rpc_xprt;`) rather than
including the heavyweight kernel headers — both because of the
"unknown order" concern and because the force-include fires before
the kernel headers are even visible.

## 9.2 The kernel-version gate

Most shims live inside one outer block:

```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 8, 0)
   ...
#endif /* >= 6.8.0 */
```

The gate exists for a single forward-looking reason: if someone tries
to compile this DKMS package against a kernel older than 6.8 (e.g.
Ubuntu 22.04's 5.15), the shims do not apply because the upstream
APIs they paper over had different shapes. Compiling on such a
kernel would still likely fail, but at least the failure mode would
be "missing symbol" rather than "shim collides with stock". The
project does not currently support anything below Ubuntu 24.04 LTS
(kernel 6.8), and the gate documents that boundary at the
preprocessor level.

A nested gate handles drift *within* the supported range:

```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
   ... xprt_iter_get_xprt approximation ...
#endif
```

That one is covered in §9.10.

## 9.3 `NFSDBG_ENFS`

```c
#ifndef NFSDBG_ENFS
#define NFSDBG_ENFS 0x10000
#endif
```

OpenEuler adds `NFSDBG_ENFS = 0x10000` to
`include/uapi/linux/nfs_fs.h`. Stock Ubuntu does not. enfs source
references it via `ifdebug(ENFS)` and `nfs_debug & NFSDBG_ENFS`
(typical pattern for the kernel's NFS debug-mask facility — set bits
in `/proc/sys/sunrpc/nfs_debug` to enable categories of `dprintk`).

The shim defines the bit at the compat-header level rather than
patching the UAPI header. The reason is the same as for
`NFS3PROC_EXTEND` (§9.4): patching UAPI is strictly avoided in this
package. UAPI changes ripple into every userspace consumer of the
header (tools, libraries, even `mount.nfs`), and the cost of getting
that wrong is much higher than the cost of a compat-header
`#define`.

The bit value is chosen to match OpenEuler's so that any saved
`/proc/sys` debug-mask values keep their meaning across migration
between an OE kernel and our DKMS-on-Ubuntu setup.

**Classification:** true compatibility shim — no behavioural change,
just makes a missing symbol available at compile time.

## 9.4 `NFS3PROC_EXTEND`

```c
#ifndef NFS3PROC_EXTEND
#define NFS3PROC_EXTEND 22
#endif
```

The proper home for this constant is `include/uapi/linux/nfs3.h`.
Patch 0012 (chapter 8) defines it locally inside `fs/nfs/nfs3xdr.c`
because the encoder/decoder pair lives there. enfs's
`fs/nfs/enfs/exten_call.c` *also* needs to see the value — it
indexes into `nfs3_procedures[NFS3PROC_EXTEND]`. Defining it at the
compat-header level guarantees both the encoder side and the caller
side agree without each having to re-`#define` it.

The value matches OpenEuler's UAPI header
(`vendor/openeuler/include/uapi/linux/nfs3.h`).

**Classification:** true compatibility shim.

## 9.5 `rpc_task_get_next_xprt` forward declaration

```c
struct rpc_xprt *rpc_task_get_next_xprt(struct rpc_clnt *clnt);
```

Patch 0016 drops `static` from this function in
`net/sunrpc/clnt.c` and adds `EXPORT_SYMBOL_GPL`. But because the
function was previously file-private, the kernel header
`<linux/sunrpc/clnt.h>` carries no declaration for it. enfs's
`failover_path.c` would otherwise see only an implicit declaration
(or, on a strict-prototype build, fail entirely). The shim provides
the declaration so callers across the module boundary can take its
address and call it normally.

**Classification:** true compatibility shim — pairs with a real
patch (0016), zero behavioural change.

## 9.6 `xprt_switch_add_xprt_locked` forward declaration

```c
struct rpc_xprt;
struct rpc_xprt_switch;
void xprt_switch_add_xprt_locked(struct rpc_xprt_switch *xps,
                                 struct rpc_xprt *xprt);
```

Same story as §9.5. Patch 0017 exports
`xprt_switch_add_xprt_locked` (it was `static` in stock); the kernel
header doesn't declare it, so we declare it here. The forward
`struct` declarations are necessary because the compat header
deliberately doesn't `#include <linux/sunrpc/xprtmultipath.h>` —
that header is brought in by patch 0017's edits to
`xprtmultipath.c`, and we do not want the compat header to depend on
patch ordering.

**Classification:** true compatibility shim.

## 9.7 `rpc_clnt_test_xprt`

```c
struct rpc_call_ops;
int rpc_clnt_test_xprt(struct rpc_clnt *clnt, struct rpc_xprt *xprt,
                       const struct rpc_call_ops *ops, void *data, int flags);
```

Patch 0020 re-implements this older OE API in `clnt.c` (mainline
dropped it in favour of `rpc_clnt_setup_test_and_add_xprt`). The
declaration sits in the compat header so that callers in enfs source
(`pm_ping.c:321, 568`) see the prototype without needing a
coordinated edit to `<linux/sunrpc/clnt.h>`.

**Classification:** true compatibility shim — pairs with patch 0020.

## 9.8 `rpc_localalladdr`: graceful-degradation stub

```c
static inline size_t enfs_compat_rpc_localalladdr(struct rpc_xprt *xprt,
        struct sockaddr *buf, size_t buflen)
{
    (void)xprt; (void)buf; (void)buflen;
    WARN_ONCE(1, "enfs: rpc_localalladdr stubbed (auto-bind unavailable; use explicit localaddrs=)");
    return 0;
}
#define rpc_localalladdr(x, b, l) enfs_compat_rpc_localalladdr(x, b, l)
```

OpenEuler has a real `rpc_localalladdr()` helper that enumerates the
local NIC source addresses available for the
`localaddrs=` mount option's "auto-bind" mode (the user writes
`localaddrs=auto` and the kernel populates the list from local
interfaces). Stock Ubuntu has no equivalent.

The shim returns 0 (zero addresses found), which causes enfs callers
to fall through to the explicit IP list provided by the user.
Mounts with `localaddrs=` *and* an explicit address list still work;
the only feature lost is the auto-bind shortcut.

`WARN_ONCE` ensures the shim's first invocation produces a single
dmesg line, so an admin who *was* relying on auto-bind sees a
message explaining what to do instead. Subsequent invocations stay
silent.

**Classification:** graceful-degradation stub. Loses an OE
optimisation; preserves correctness.

## 9.9 `shard_route` stubs (six functions)

Six identifier-only `#define`s redirecting `enfs_delete_clnt_shard_cache`,
`shard_set_transport`, `enfs_shard_init`, `enfs_shard_exit`,
`enfs_query_xprt_shard`, and `enfs_print_uuid` to no-op
`enfs_compat_*` inline functions.

OpenEuler's `fs/nfs/enfs/shard_route.c` implements per-file sharding
across NLM (file-locking) endpoints. The subsystem depends on lockd
patches this DKMS package has not done yet. Rather than ship
half-implemented, `shard_route.o` is dropped from `enfs-y`
([`Kbuild`](../../Kbuild) line 159+). Other enfs files still call
into `shard.h`'s symbols, so without stubs the linker would fail
with six `undefined reference` errors. The stubs let the link
succeed; runtime simply lacks NLM multipath and per-file UUID
display in `/proc/enfs/`.

The `#define X enfs_compat_X` form (identifier only, *not*
`#define X(args) enfs_compat_X(args)`) is deliberate, noted in the
compat header itself: `enfs_init.c` takes the address of some of
these symbols (`&enfs_shard_init`, ...) for a function-pointer init
table. Function-call-site `#define`s break `&X` and bare `X` in
non-call contexts; the identifier form preserves both.

**Classification:** graceful-degradation stubs + TODO. Locking
multipath unavailable; non-locking multipath works. Drop the stubs
and re-enable `shard_route.o` once lockd patches land.

## 9.10 `rpc_xprt_switch_set_singular`: graceful-degradation stub

```c
static inline void enfs_compat_rpc_xprt_switch_set_singular(struct rpc_xprt_switch *xps)
{
    (void)xps;
    WARN_ONCE(1, "enfs: rpc_xprt_switch_set_singular stubbed (Ubuntu 7.0 has no singular iter)");
}
#define rpc_xprt_switch_set_singular(xps) enfs_compat_rpc_xprt_switch_set_singular(xps)
```

OpenEuler's `xprtmultipath.c` ships an iterator-ops variant called
`rpc_xprt_iter_singular` that always returns the same xprt. Its
public setter (`rpc_xprt_switch_set_singular`) is called from
`enfs_roundrobin.c` during failover-pinning to lock all subsequent
RPCs on a client to one specific path.

Stock Ubuntu has only `rpc_xprt_iter_roundrobin` and the older
`rpc_xprt_iter_singular` *type* but no public setter. The shim
no-ops — the singular pinning behaviour is silently skipped, but
round-robin continues to work and the client remains functional.

The compat header itself flags the shim as a TODO: "replicate OE's
rpc_xprt_iter_singular by porting it into compat/ as a small
standalone iter_ops". Until that is done, this is the same shape as
§9.8 and §9.9 — graceful degradation with a `WARN_ONCE` so the user
finds out the first time it would have mattered.

**Classification:** graceful-degradation stub + TODO. Loses an
OE-specific failover optimisation. Failover still happens; it just
re-rotates rather than pinning to a single path.

## 9.11 `xprt_iter_get_xprt`: kernel-version-gated approximation {#910-xprt_iter_get_xprt}

```c
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)
static inline struct rpc_xprt *enfs_compat_xprt_iter_get_xprt(struct rpc_xprt_iter *xpi)
{
    return xprt_iter_get_next(xpi);
}
#define xprt_iter_get_xprt(xpi) enfs_compat_xprt_iter_get_xprt(xpi)
#endif
```

OpenEuler's `xprt_iter_get_xprt()` returns the xprt currently pointed
to by the iterator cursor *without advancing*. Ubuntu 6.8 still
ships this helper natively (and the 6.8 patch series exports it via
patch 0017 — see [chapter 8 §8.8](./08-patch-series.md#88-sidebar-per-target-rebase-variants)).
Ubuntu 6.14 and 7.0 dropped it upstream. The shim approximates it
using `xprt_iter_get_next` (which *does* advance).

The semantic difference is one extra advance per call. For a
round-robin iterator, that means a slight load shift — instead of
"return the same xprt I last gave you", the caller gets the *next*
one. enfs only consults this helper to find "*some* live xprt",
never specifically the current one, so correctness is preserved.

The compat-header comment notes this as a TODO: "if a benchmark
shows noticeable load imbalance, revisit by porting OE's
xprt_iter_get_helper() into compat/." The benchmark has not been
done; the shim has been adequate in smoke testing.

The `#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 14, 0)` guard
ensures that on 6.8 (which has the real helper) the shim does *not*
override the kernel symbol. Without the guard, the shim would
shadow the real helper and force the (slightly worse) approximation
on a kernel that doesn't need it.

**Classification:** graceful-degradation shim, with a kernel-version
gate so the degradation only applies where the real helper is
absent. The 6.8 build uses the kernel's own helper unmodified.

## 9.12 Why a single `>= 6.8.0` block holds everything

Ubuntu 6.8, 6.11, 6.14, and 7.0 all need the same set of compat
shims with one exception (the 6.14-and-up `xprt_iter_get_xprt`
approximation in §9.11). The compat header consolidates the rest
into a single `>= 6.8.0` block on the rationale stated in the file
itself:

> Each helper either declares a function our patches export (so the
> upstream header doesn't need to change), or stubs an OpenEuler-only
> helper that has no Ubuntu equivalent. None of them clash with stock
> Ubuntu symbols on any supported version, so a single block covers
> all targets.

Concretely:

- The forward declarations (§§9.5–9.7) are no-ops if the symbol is
  already declared. Worst case the compiler sees the same prototype
  twice, which is legal as long as it agrees with itself.
- The graceful-degradation stubs (§§9.8, 9.10) define helpers that
  do not exist on any supported Ubuntu kernel. There is no symbol to
  collide with.
- The shard stubs (§9.9) define helpers that don't exist anywhere
  outside OE. Again, no collision.

If a future Ubuntu kernel ever *adds* one of the stubbed symbols
(e.g. mainline finally adds a `rpc_xprt_switch_set_singular`), the
shim would need to grow a kernel-version gate around it before that
kernel becomes a build target. CI would fail the new target's first
build — so the failure mode is at least loud.

## 9.13 What the compat layer does *not* do

Counterpoints to keep the model honest:

- **No struct layout changes.** Those go through patches 0005, 0007,
  0009 with `__GENKSYMS__` guards. A struct field cannot be added
  by `#define`.
- **No symbol exports.** Exports go through the patch series
  (0016–0018). The compat header can declare a symbol enfs links
  to, but `EXPORT_SYMBOL_GPL` must live in patched code.
- **No function bodies for missing exports.** Patch 0020
  (re-implementing `rpc_clnt_test_xprt`) lives in the patch series
  because it must be compiled into `sunrpc.ko`. The compat header
  only declares it.
- **No insertions into `clnt.c` or `xprt.c`.** Hooks like patches
  0013 / 0014 are out of scope.

The dividing line: **patches mutate kernel files; the compat header
adds C declarations and tiny inline helpers visible everywhere.**

## 9.14 Audit: every shim, every classification

Quick reference table. Cross-references point at the section above.

| Shim / declaration | Section | Classification | Pairs with |
|---|---|---|---|
| `NFSDBG_ENFS` | §9.3 | true compat | none (UAPI gap) |
| `NFS3PROC_EXTEND` | §9.4 | true compat | patch 0012 |
| `rpc_task_get_next_xprt` decl | §9.5 | true compat | patch 0016 |
| `xprt_switch_add_xprt_locked` decl | §9.6 | true compat | patch 0017 |
| `rpc_clnt_test_xprt` decl | §9.7 | true compat | patch 0020 |
| `rpc_localalladdr` stub | §9.8 | graceful-degradation | none |
| `enfs_delete_clnt_shard_cache` stub | §9.9 | graceful-degradation + TODO | (lockd patches not yet written) |
| `shard_set_transport` stub | §9.9 | graceful-degradation + TODO | same |
| `enfs_shard_init` / `_exit` stubs | §9.9 | graceful-degradation + TODO | same |
| `enfs_query_xprt_shard` stub | §9.9 | graceful-degradation + TODO | same |
| `enfs_print_uuid` stub | §9.9 | graceful-degradation + TODO | same |
| `rpc_xprt_switch_set_singular` stub | §9.10 | graceful-degradation + TODO | none |
| `xprt_iter_get_xprt` approx | §9.11 | graceful-degradation, gated | patch 0017 (different by target) |

Anything that lands in this header in future should fit one of
those three classifications and update this table.
