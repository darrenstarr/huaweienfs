# 0004 — `net/sunrpc/Kconfig`: add `CONFIG_SUNRPC_ENFS`

Patch file: [`patches/ubuntu-7.0/0004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.patch`](../../patches/ubuntu-7.0/0004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.patch)

## What this change adds to stock Linux

A single Kconfig stanza is appended to `net/sunrpc/Kconfig` to gate
the SunRPC-side adapter:

**Before** (last lines of `net/sunrpc/Kconfig`, Ubuntu 7.0 stock):

```kconfig
config SUNRPC_XPRT_RDMA
        ...
          If unsure, or you know there is no RDMA capability on your
          hardware platform, say N.
```

**After**:

```kconfig
config SUNRPC_XPRT_RDMA
        ...
          If unsure, or you know there is no RDMA capability on your
          hardware platform, say N.

config SUNRPC_ENFS
        bool "sunrpc support ENFS"
        depends on SUNRPC
        depends on X86 || X86_64 || ARM64
        default n
        help
         This option enables support multipath of the NFS protocol
         in the kernel's NFS client.
         This feature will improve performance and reliability.

         If sure, say Y.
```

## Where it lives

- File: `net/sunrpc/Kconfig`
- Insertion point: end of file, after `config SUNRPC_XPRT_RDMA`.
- Pure append.

## Why it's needed

`CONFIG_SUNRPC_ENFS` is the SunRPC counterpart to `CONFIG_ENFS`.
Three places in the patch series gate on it:

1. Patch 0002:
   `sunrpc-$(CONFIG_SUNRPC_ENFS) += sunrpc_enfs_adapter.o` — without
   the symbol, no adapter object would compile into `sunrpc.ko` and
   `enfs.ko` would fail to resolve `rpc_multipath_ops_register` at
   load time.
2. Patch 0005's two new `struct rpc_clnt` fields (`cl_enfs:1` and
   `multipath_option`) are wrapped in
   `#if IS_ENABLED(CONFIG_SUNRPC_ENFS)`. Without the Kconfig symbol
   the `#if` is always false and the fields don't exist.
3. Patch 0006's `RPC_TASK_ENFS` / `RPC_TASK_FIXED` `#define`s are
   wrapped the same way.

It is `bool` (not `tristate`) because all the conditional code lives
inside `sunrpc.ko`, which is itself tristate; the symbol just gates
whether the adapter sources participate in that build.

The `select SUNRPC_ENFS` from `config ENFS` (patch 0003) means a user
who enables `ENFS` automatically gets `SUNRPC_ENFS` — they do not
need to know the two-module split.

## Why this exact form

Verbatim from `vendor/openeuler/net/sunrpc/Kconfig`. The arch
restriction (`X86 || X86_64 || ARM64`) intentionally mirrors
`config ENFS` so the two halves cannot drift out of agreement.

## Observable effect for users

A new menu entry appears under
`Networking support → Networking options → Sun RPC`:

```text
[ ] sunrpc support ENFS
```

For DKMS builds the symbol is set non-interactively by the package.
End users who never run `menuconfig` see no difference.
