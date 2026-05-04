# tests/kernel-shim/

Userspace fakes for the kernel headers that `vendor/openeuler/fs/nfs/enfs/*.c`
files include. The build prepends `-I tests/kernel-shim` so these
files shadow real `linux/*.h` from the system, and force-includes
`linux/shim_bootstrap.h` so kernel-only macros (`__KERNEL__`, etc.)
are defined before any source compiles.

## Layout

Each fake lives at the same path that the kernel uses, e.g.
`linux/spinlock.h` so `#include <linux/spinlock.h>` finds it.

## Design rules

1. **Only fake what's needed.** Every shim adds maintenance surface.
   If no source-under-test includes it (directly or transitively),
   it doesn't exist here.
2. **Behaviorally faithful for single-threaded tests.** RCU, locks,
   and atomics are no-ops or trivial implementations because tests
   run single-threaded. If we ever test concurrency we revisit.
3. **No silent surprises.** If a function is faked as a stub that
   does nothing, name it clearly (e.g. `pr_info` → `printf`, not
   silently dropped). A stubbed function that *should* fail loudly
   when called calls `__shim_unimplemented(__func__)`.
4. **Match the kernel's signatures.** Don't take shortcuts on
   parameter types — if the production source calls
   `kref_read(struct kref *)` we provide that exact signature.

## Files

| File | Fakes | Source-under-test that needs it |
|---|---|---|
| `shim_bootstrap.h` | `__KERNEL__`, common compiler attrs, BUILD_BUG_ON | (force-included by Makefile) |
| `linux/types.h` | `__be32`, `u32`, `bool` etc. | all |
| `linux/kernel.h` | `READ_ONCE`, `WRITE_ONCE`, `container_of`, `min`/`max` | all |
| `linux/compiler.h` | `__rcu`, `__force`, `__must_check` annotations | all |
| `linux/printk.h` | `pr_info`, `pr_err`, `printk` → printf | all |
| `linux/module.h` | `MODULE_*`, `EXPORT_SYMBOL*` → no-ops | all |
| `linux/spinlock.h` | `spinlock_t` → pthread_mutex_t | enfs_roundrobin |
| `linux/atomic.h` | `atomic_t`, `atomic_long_t` → C11 stdatomic | enfs_roundrobin |
| `linux/list.h` | `struct list_head`, `list_for_each_entry` | enfs_roundrobin |
| `linux/rculist.h` | `list_for_each_entry_rcu` (= non-rcu version) | enfs_roundrobin |
| `linux/rcupdate.h` | `rcu_read_lock`/`unlock`, `rcu_dereference` → no-op | enfs_roundrobin |
| `linux/kref.h` | `struct kref`, `kref_read`, `kref_init` | enfs_roundrobin |
| `linux/refcount.h` | `refcount_t` → atomic | (transitive) |
| `linux/slab.h` | `kmalloc`, `kfree` → malloc/free | (as needed) |
| `linux/kabi.h` | `KABI_RESERVE()` → empty | (transitive) |
| `linux/sunrpc/xprt.h` | minimal `struct rpc_xprt` (fields enfs touches) | enfs_roundrobin |
| `linux/sunrpc/clnt.h` | minimal `struct rpc_clnt` | enfs_roundrobin |
| `linux/sunrpc/xprtmultipath.h` | `struct rpc_xprt_switch`, iter ops | enfs_roundrobin |

When a new source file lands in `unit/`, expand this table.
