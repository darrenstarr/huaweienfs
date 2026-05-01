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
| 0007 | [`0007-include-nfs_fs_sb-add-enfs-fields.patch`](../../patches/ubuntu-7.0/0007-include-nfs_fs_sb-add-enfs-fields.patch) | [`007-include-nfs_fs_sb-add-enfs-fields.md`](007-include-nfs_fs_sb-add-enfs-fields.md) | uAPI/struct layout | medium (struct grows, hidden from genksyms) |
| 0008 | [`0008-include-nfs_xdr-add-extend-xdr-arg.patch`](../../patches/ubuntu-7.0/0008-include-nfs_xdr-add-extend-xdr-arg.patch) | [`008-include-nfs_xdr-add-extend-xdr-arg.md`](008-include-nfs_xdr-add-extend-xdr-arg.md) | NFSv3 wire format | medium (non-standard NFSv3 op) |
| 0009 | [`0009-fs-nfs-internal-add-enfs-option-fields.patch`](../../patches/ubuntu-7.0/0009-fs-nfs-internal-add-enfs-option-fields.patch) | [`009-fs-nfs-internal-add-enfs-option-fields.md`](009-fs-nfs-internal-add-enfs-option-fields.md) | private mount-context structs | trivial |
| 0010 | [`0010-fs-nfs-super-add-enfs-hooks.patch`](../../patches/ubuntu-7.0/0010-fs-nfs-super-add-enfs-hooks.patch) | [`010-fs-nfs-super-add-enfs-hooks.md`](010-fs-nfs-super-add-enfs-hooks.md) | `fs/nfs` superblock | low |
| 0011 | [`0011-fs-nfs-fs_context-add-enfs_info-mount-option.patch`](../../patches/ubuntu-7.0/0011-fs-nfs-fs_context-add-enfs_info-mount-option.patch) | [`011-fs-nfs-fs_context-add-enfs_info-mount-option.md`](011-fs-nfs-fs_context-add-enfs_info-mount-option.md) | mount-option parser | medium (largest patch in series) |
| 0012 | [`0012-fs-nfs-nfs3xdr-extend-call.patch`](../../patches/ubuntu-7.0/0012-fs-nfs-nfs3xdr-extend-call.patch) | [`012-fs-nfs-nfs3xdr-extend-call.md`](012-fs-nfs-nfs3xdr-extend-call.md) | NFSv3 XDR | medium (adds non-standard op) |
| 0013 | [`0013-net-sunrpc-clnt-multipath-hooks.patch`](../../patches/ubuntu-7.0/0013-net-sunrpc-clnt-multipath-hooks.patch) | [`013-net-sunrpc-clnt-multipath-hooks.md`](013-net-sunrpc-clnt-multipath-hooks.md) | SunRPC client core | medium (8 hook sites; 2 deferred) |
| 0014 | [`0014-net-sunrpc-xprt-multipath-hooks.patch`](../../patches/ubuntu-7.0/0014-net-sunrpc-xprt-multipath-hooks.patch) | [`014-net-sunrpc-xprt-multipath-hooks.md`](014-net-sunrpc-xprt-multipath-hooks.md) | SunRPC xprt layer | medium (7 hook sites) |
| 0015 | [`0015-fs-nfs-internal-add-include-guard.patch`](../../patches/ubuntu-7.0/0015-fs-nfs-internal-add-include-guard.patch) | [`015-fs-nfs-internal-add-include-guard.md`](015-fs-nfs-internal-add-include-guard.md) | header hygiene | trivial |
| 0016 | [`0016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.patch`](../../patches/ubuntu-7.0/0016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.patch) | [`016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.md`](016-net-sunrpc-clnt-export-rpc_task_get_next_xprt.md) | symbol export | trivial |
| 0017 | [`0017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.patch`](../../patches/ubuntu-7.0/0017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.patch) | [`017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.md`](017-net-sunrpc-xprtmultipath-export-helpers-for-enfs.md) | symbol exports (5) | trivial |
| 0018 | [`0018-sunrpc-nfs-export-helpers-for-enfs.patch`](../../patches/ubuntu-7.0/0018-sunrpc-nfs-export-helpers-for-enfs.patch) | [`018-sunrpc-nfs-export-helpers-for-enfs.md`](018-sunrpc-nfs-export-helpers-for-enfs.md) | symbol exports (2) | trivial |
| 0019 | [`0019-fs-nfs-client-propagate-enfs-option.patch`](../../patches/ubuntu-7.0/0019-fs-nfs-client-propagate-enfs-option.patch) | [`019-fs-nfs-client-propagate-enfs-option.md`](019-fs-nfs-client-propagate-enfs-option.md) | `fs/nfs` client | medium (3 hooks where OE had 2) |
| 0020 | [`0020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.patch`](../../patches/ubuntu-7.0/0020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.patch) | [`020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.md`](020-net-sunrpc-clnt-add-rpc_clnt_test_xprt.md) | SunRPC client API | low (re-adds removed API) |

When in doubt about scope, open the matching `.diff` under
`vendor/openeuler/` and use `grep -rn` to confirm runtime usage from
`vendor/openeuler/fs/nfs/enfs/`.
