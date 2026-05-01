# 0002 — `net/sunrpc/Makefile`: link `sunrpc_enfs_adapter.o` into `sunrpc.ko`

Patch file: [`patches/ubuntu-7.0/0002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.patch`](../../patches/ubuntu-7.0/0002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.patch)

## What this change adds to stock Linux

A single line is appended to the SunRPC Makefile so that, when
`CONFIG_SUNRPC_ENFS=y`, the source file `sunrpc_enfs_adapter.c` is
compiled and linked into `sunrpc.ko`.

**Before** (`net/sunrpc/Makefile`, last few lines, Ubuntu 7.0 stock):

```c
sunrpc-$(CONFIG_SUNRPC_DEBUG)        += debugfs.o
sunrpc-$(CONFIG_SUNRPC_BACKCHANNEL)  += backchannel_rqst.o
sunrpc-$(CONFIG_PROC_FS)             += stats.o
sunrpc-$(CONFIG_SYSCTL)              += sysctl.o
```

**After** (with this patch):

```c
sunrpc-$(CONFIG_SUNRPC_DEBUG)        += debugfs.o
sunrpc-$(CONFIG_SUNRPC_BACKCHANNEL)  += backchannel_rqst.o
sunrpc-$(CONFIG_PROC_FS)             += stats.o
sunrpc-$(CONFIG_SYSCTL)              += sysctl.o
sunrpc-$(CONFIG_SUNRPC_ENFS)         += sunrpc_enfs_adapter.o
```

## Where it lives

- File: `net/sunrpc/Makefile`
- Insertion point: end of file, after the existing `sunrpc-$(...)`
  lines.
- One-liner. No existing line is modified.

## Why it's needed

`sunrpc_enfs_adapter.c` provides the SunRPC half of the registry that
glues `enfs.ko` to the kernel transport layer. Specifically, it
declares and exports:

```c
/* vendor/openeuler/net/sunrpc/sunrpc_enfs_adapter.c */
struct rpc_multipath_ops __rcu *multipath_ops;

int rpc_multipath_ops_register(struct rpc_multipath_ops *ops);     /* line 22 */
int rpc_multipath_ops_unregister(struct rpc_multipath_ops *ops);   /* line 34 */
struct rpc_multipath_ops *rpc_multipath_ops_get(void);             /* line 46 */
void rpc_multipath_ops_put(struct rpc_multipath_ops *ops);         /* line 63 */
```

…plus thin wrappers (`rpc_multipath_ops_create_clnt`,
`rpc_multipath_ops_releas_clnt`, `rpc_multipath_ops_inc_queuelen`,
`rpc_multipath_ops_dec_queuelen`, `rpc_multipath_ops_create_xprt`)
that the patched `clnt.c` and `xprt.c` will call from inside the
SunRPC dispatch path.

`enfs.ko` registers its concrete ops at `module_init`:

```c
/* vendor/openeuler/fs/nfs/enfs/enfs_multipath.c, line 1083 */
rpc_multipath_ops_register(&ops);
```

The registry has to live in `sunrpc.ko` itself, not in `enfs.ko`,
because the SunRPC dispatch path calls into it on every RPC and
cannot tolerate being a forward declaration that an out-of-tree
module *might* later resolve. Linking `sunrpc_enfs_adapter.o` into
`sunrpc.ko` is what makes the symbols `rpc_multipath_ops_register`
and `rpc_multipath_ops_get` resolvable when `enfs.ko` is loaded.

## Why this exact form

Identical to OpenEuler's own line (compare
`vendor/openeuler/net/sunrpc/Makefile:22`). Keeping it verbatim
makes future re-syncs against newer OE drops a no-op for this file.

## Observable effect for users

None on its own. With `CONFIG_SUNRPC_ENFS=n` (default), the patch
adds nothing. With `CONFIG_SUNRPC_ENFS=y`, `sunrpc.ko` grows by the
size of `sunrpc_enfs_adapter.o` (~6 KB compiled) and exports the
`rpc_multipath_ops_*` symbols — visible via
`grep rpc_multipath_ops /proc/kallsyms` — but no behaviour changes
until `enfs.ko` actually registers ops.
