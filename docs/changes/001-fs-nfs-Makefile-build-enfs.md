# 0001 — `fs/nfs/Makefile`: build `enfs_adapter.o` into `nfs.ko` and descend into `enfs/`

Patch file: [`patches/ubuntu-7.0/0001-fs-nfs-Makefile-build-enfs.patch`](../../patches/ubuntu-7.0/0001-fs-nfs-Makefile-build-enfs.patch)

## What this change adds to stock Linux

Two build hooks are appended to the stock NFS Makefile so that, when
`CONFIG_ENFS=m` (or `=y`), the kernel build system

1. links `enfs_adapter.o` into the `nfs.ko` aggregate, and
2. descends into the new `fs/nfs/enfs/` subdirectory and builds
   `enfs.ko` there.

**Before** (`fs/nfs/Makefile`, last few lines, Ubuntu 7.0 stock):

```c
obj-$(CONFIG_PNFS_FILE_LAYOUT)     += filelayout/
obj-$(CONFIG_PNFS_BLOCK)           += blocklayout/
obj-$(CONFIG_PNFS_FLEXFILE_LAYOUT) += flexfilelayout/
```

**After** (with this patch):

```c
obj-$(CONFIG_PNFS_FILE_LAYOUT)     += filelayout/
obj-$(CONFIG_PNFS_BLOCK)           += blocklayout/
obj-$(CONFIG_PNFS_FLEXFILE_LAYOUT) += flexfilelayout/

ifneq ($(CONFIG_ENFS),)
        nfs-y += enfs_adapter.o
endif
obj-$(CONFIG_ENFS) += enfs/
```

## Where it lives

- File: `fs/nfs/Makefile`
- Insertion point: end of file, after the `PNFS_*` `obj-$(...)` lines.
- No existing line is modified; this is pure append.

## Why it's needed

`enfs.ko` is not self-contained. It plugs into `nfs.ko` through the
`enfs_adapter_ops` registry (`enfs_adapter_register()` /
`enfs_adapter_unregister()`), which lives in `enfs_adapter.c`. The
adapter source has to be linked **into** `nfs.ko` itself — it cannot
be in a separate module — because the in-tree NFS code calls into the
registered ops directly (e.g. `enfs_trigger_get_capability` is invoked
from `fs/nfs/super.c`). The corresponding registration in
`enfs.ko` lives in:

```c
/* vendor/openeuler/fs/nfs/enfs/enfs_init.c, line 97 */
ret = enfs_adapter_register(&enfs_adapter);
```

So `nfs.ko` provides the registry; `enfs.ko` registers into it at
`module_init` time. Both halves must build, hence both lines of this
patch. The OE upstream Makefile carries the same two stanzas
verbatim (compare `vendor/openeuler/fs/nfs/Makefile`).

The two source files referenced by this Makefile change —
`enfs_adapter.c` and the `enfs/` subdirectory — are dropped into
`fs/nfs/` at src-tree-build time by `scripts/build-src-tree.sh`; this
patch only adds the build instructions for them.

## Why this exact form

The `ifneq ($(CONFIG_ENFS),)` guard around `nfs-y += enfs_adapter.o`
matches OpenEuler's wording rather than the more conventional
`nfs-$(CONFIG_ENFS) += enfs_adapter.o`. The two are equivalent for
tristate symbols, but we preserve the OE form to keep this patch a
verbatim port — easier to re-sync when the upstream Makefile drifts.

## Observable effect for users

None on its own. This is build-system plumbing — when
`CONFIG_ENFS=n` (the default), the patch contributes zero bytes to
`nfs.ko` and nothing to the `obj-` list. With `CONFIG_ENFS=m`, the
DKMS build produces `nfs.ko` (now containing `enfs_adapter.o`) and a
new `enfs.ko` under `fs/nfs/enfs/`. Both end up in
`/lib/modules/$(uname -r)/updates/` and `modprobe enfs` becomes
possible.
