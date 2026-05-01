# 0003 — `fs/nfs/Kconfig`: add `CONFIG_ENFS` and `CONFIG_ENFS_KUNIT_TEST`

Patch file: [`patches/ubuntu-7.0/0003-fs-nfs-Kconfig-add-CONFIG_ENFS.patch`](../../patches/ubuntu-7.0/0003-fs-nfs-Kconfig-add-CONFIG_ENFS.patch)

## What this change adds to stock Linux

Two `Kconfig` stanzas are appended to `fs/nfs/Kconfig`:

- `config ENFS` — a tristate gate for the enfs feature, depending on
  `NFS_FS` and one of `X86 / X86_64 / ARM64`. It `select`s
  `SUNRPC_ENFS` (added by patch 0004) so the SunRPC half auto-enables.
- `config ENFS_KUNIT_TEST` — gated by `KUNIT`, builds the in-tree
  KUnit tests under `fs/nfs/enfs/`.

**Before** (last lines of `fs/nfs/Kconfig`, Ubuntu 7.0 stock):

```kconfig
config NFS_V4_2_READ_PLUS
        bool "NFS: Enable support for the NFSv4.2 READ_PLUS operation"
        depends on NFS_V4_2
        default y
        help
         Choose Y here to enable use of the NFS v4.2 READ_PLUS operation.
```

**After**:

```kconfig
config NFS_V4_2_READ_PLUS
        bool "NFS: Enable support for the NFSv4.2 READ_PLUS operation"
        depends on NFS_V4_2
        default y
        help
         Choose Y here to enable use of the NFS v4.2 READ_PLUS operation.

config ENFS
        tristate "NFS client support for ENFS"
        depends on NFS_FS
        depends on X86 || X86_64 || ARM64
        select SUNRPC_ENFS
        default n
        help
         This option enables support multipath of the NFS protocol
         in the kernel's NFS client.
         This feature will improve performance and reliability.

         If sure, say Y.

config ENFS_KUNIT_TEST
        bool "This builds the ENFS KUnit tests" if !KUNIT_ALL_TESTS
        depends on KUNIT
        default KUNIT_ALL_TESTS
        help
          Only useful for kernel devs running KUnit test harness and are not
          for inclusion into a production build.

          For more information on KUnit and unit tests in general please refer
          to the KUnit documentation in Documentation/dev-tools/kunit/.

          If unsure, say N.
```

## Where it lives

- File: `fs/nfs/Kconfig`
- Insertion point: end of file, after `config NFS_V4_2_READ_PLUS`.
- Pure append; no existing stanza is touched.

## Why it's needed

`CONFIG_ENFS` is the master switch the rest of the patch series keys
off:

- Patch 0001 uses `obj-$(CONFIG_ENFS) += enfs/` and a guarded
  `nfs-y += enfs_adapter.o`.
- Patch 0005's struct fields are wrapped in
  `#if IS_ENABLED(CONFIG_SUNRPC_ENFS)` (the SunRPC counterpart).
- Patch 0006's `RPC_TASK_ENFS` / `RPC_TASK_FIXED` defines are
  similarly guarded.

Without this Kconfig symbol, the build never enters any of those
stanzas. The `select SUNRPC_ENFS` arrow ensures a user choosing
`CONFIG_ENFS` does not have to find and enable the SunRPC half by
hand.

The dependency on `X86 || X86_64 || ARM64` mirrors OpenEuler's
restriction. enfs has not been smoke-tested on RISC-V / PowerPC, and
some of the per-arch atomic patterns in `shard_route.c` have not been
audited there.

## Why this exact form

The stanzas are pasted **verbatim** from
`vendor/openeuler/fs/nfs/Kconfig` (the OE base file already contains
both). We deliberately avoid editorialising the help text or the
default; doing so would add gratuitous review surface every time we
re-sync from upstream. The KUnit stanza is included even though we
do not currently build the tests, so that re-enabling them later does
not require yet another patch.

## Observable effect for users

`make menuconfig` / `make nconfig` will show two new menu entries
under `File systems → Network File Systems → NFS client support`:

- `[ ] NFS client support for ENFS`
- `[ ] This builds the ENFS KUnit tests`

For DKMS builds, neither is presented interactively — the DKMS
package sets `CONFIG_ENFS=m` directly via the
`KCFLAGS`/`KBUILD_EXTMOD` invocation. End users who never run
`menuconfig` see no difference.
