# Stock-vs-eNFS prose change log

This directory has one markdown doc per patch under
`patches/ubuntu-7.0/`. Each doc explains, in plain English with
inline before/after snippets, what the patch adds to the stock Ubuntu
26.04 (kernel 7.0) tree, where it lives, why it is needed, why it
takes its specific form (especially where we deviate from OpenEuler's
original), and what — if anything — a user or operator would observe.

These docs replace the earlier `docs/differences/` raw `.diff` dump,
which gave the file-by-file delta but no explanation of intent.

The audience is a kernel developer reviewing whether to ship this
DKMS package in production. Read in numeric order — the build-glue
patches (0001–0004) come first, then the public-header changes that
let `enfs.ko` link against the patched `nfs.ko` / `sunrpc.ko`
(0005–0006).

## Index

| # | Patch file | Doc | Subsystem | Risk |
|---|---|---|---|---|
| 0001 | [`0001-fs-nfs-Makefile-build-enfs.patch`](../../patches/ubuntu-7.0/0001-fs-nfs-Makefile-build-enfs.patch) | [`001-fs-nfs-Makefile-build-enfs.md`](001-fs-nfs-Makefile-build-enfs.md) | `fs/nfs` build | trivial |
| 0002 | [`0002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.patch`](../../patches/ubuntu-7.0/0002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.patch) | [`002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.md`](002-net-sunrpc-Makefile-build-sunrpc_enfs_adapter.md) | `net/sunrpc` build | trivial |
| 0003 | [`0003-fs-nfs-Kconfig-add-CONFIG_ENFS.patch`](../../patches/ubuntu-7.0/0003-fs-nfs-Kconfig-add-CONFIG_ENFS.patch) | [`003-fs-nfs-Kconfig-add-CONFIG_ENFS.md`](003-fs-nfs-Kconfig-add-CONFIG_ENFS.md) | `fs/nfs` Kconfig | trivial |
| 0004 | [`0004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.patch`](../../patches/ubuntu-7.0/0004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.patch) | [`004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.md`](004-net-sunrpc-Kconfig-add-CONFIG_SUNRPC_ENFS.md) | `net/sunrpc` Kconfig | trivial |
| 0005 | [`0005-include-sunrpc-clnt.h-add-multipath-fields.patch`](../../patches/ubuntu-7.0/0005-include-sunrpc-clnt.h-add-multipath-fields.patch) | [`005-include-sunrpc-clnt.h-add-multipath-fields.md`](005-include-sunrpc-clnt.h-add-multipath-fields.md) | uAPI/struct layout | medium (struct grows) |
| 0006 | [`0006-include-sunrpc-sched.h-add-RPC_TASK_ENFS.patch`](../../patches/ubuntu-7.0/0006-include-sunrpc-sched.h-add-RPC_TASK_ENFS.patch) | [`006-include-sunrpc-sched.h-add-RPC_TASK_ENFS.md`](006-include-sunrpc-sched.h-add-RPC_TASK_ENFS.md) | uAPI/flag bit allocation | **high — see doc** (RPC_TASK_FIXED rebased to 0x0020 collides with RPC_CALL_MAJORSEEN in stock SunRPC) |

## Patches present in tree but not yet documented

These patches exist under `patches/ubuntu-7.0/` but their prose docs
have not been written yet. Add a paired `0NN-<shortname>.md` under
this directory and link it from the index above when you do.

| # | Patch file | Subject |
|---|---|---|
| 0007 | [`0007-include-nfs_fs_sb-add-enfs-fields.patch`](../../patches/ubuntu-7.0/0007-include-nfs_fs_sb-add-enfs-fields.patch) | `include/linux/nfs_fs_sb.h` — add enfs fields and helpers to `struct nfs_server` |
| 0008 | [`0008-include-nfs_xdr-add-extend-xdr-arg.patch`](../../patches/ubuntu-7.0/0008-include-nfs_xdr-add-extend-xdr-arg.patch) | `include/linux/nfs_xdr.h` — add `struct nfs_extend_xdr_arg` for v3 extended-call XDR |
| 0009 | [`0009-fs-nfs-internal-add-enfs-option-fields.patch`](../../patches/ubuntu-7.0/0009-fs-nfs-internal-add-enfs-option-fields.patch) | `fs/nfs/internal.h` — add `enfs_option` to `nfs_client_initdata` and `nfs_fs_context` |

## Patches still to come

The patches above (0001–0009) cover the build glue, Kconfig, and most
of the header surface that `enfs.ko` links against. Several more
patches are needed before this package will load and run end-to-end:

| # | Subject | Touches |
|---|---|---|
| 00xx | `fs/nfs/super.c` — call `enfs_trigger_get_capability` after server probe | `fs/nfs/super.c` |
| 00xx | `fs/nfs/fs_context.c` — accept `enfs_info=` mount option, hand off to adapter | `fs/nfs/fs_context.c` |
| 00xx | `fs/nfs/nfs3xdr.c` — extended-call XDR for path-state RPCs | `fs/nfs/nfs3xdr.c` |
| 00xx | `net/sunrpc/clnt.c` — per-task multipath dispatch hooks | `net/sunrpc/clnt.c` |
| 00xx | `net/sunrpc/xprt.c` — `reserve_context` and queuelen accounting hooks | `net/sunrpc/xprt.c` |
| 00xx | drop `enfs_adapter.{c,h}` and `sunrpc_enfs_adapter.{c,h}` source files into the build tree | `fs/nfs/`, `net/sunrpc/`, `include/linux/sunrpc/` (currently done by `scripts/build-src-tree.sh`, may move to a patch) |

When in doubt about scope, open the matching `.diff` under
`vendor/openeuler/` and use `grep -rn` to confirm runtime usage from
`vendor/openeuler/fs/nfs/enfs/`.
