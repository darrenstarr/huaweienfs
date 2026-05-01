# 0009 — `fs/nfs/internal.h`: add `enfs_option` to mount-context structs

Patch file: [`patches/ubuntu-7.0/0009-fs-nfs-internal-add-enfs-option-fields.patch`](../../patches/ubuntu-7.0/0009-fs-nfs-internal-add-enfs-option-fields.patch)

## What this change adds to stock Linux

Two new opaque pointer slots in the private NFS mount-context
plumbing, both pointing to `struct multipath_mount_options` (defined
inside `enfs.ko`):

- `struct nfs_client_initdata::enfs_option` — used while constructing
  the `rpc_clnt` for a new NFS client. Patch 0019 will copy the value
  from `nfs_fs_context` here, and `client.c` will hand it on to
  `rpc_create_args` (patch 0005's `multipath_option`).
- `struct nfs_fs_context::enfs_option` — owned by the per-mount
  fs_context across the mount/remount lifecycle. Allocated by
  `enfs_parse_mount_options()` (called from `fs/nfs/fs_context.c`
  after patch 0011), released by `enfs_free_mount_options(ctx)` in
  the fs_context destructor.

**Before** (`fs/nfs/internal.h`, around the tail of `nfs_client_initdata`):

```c
        struct xprtsec_parms xprtsec;
        unsigned long connect_timeout;
        unsigned long reconnect_timeout;
};
```

**After**:

```c
        struct xprtsec_parms xprtsec;
        unsigned long connect_timeout;
        unsigned long reconnect_timeout;
#if IS_ENABLED(CONFIG_ENFS)
        void *enfs_option;      /* struct multipath_mount_options * */
#endif
};
```

And, in `nfs_fs_context` (around the `clone_data` block):

```c
        struct {
                struct dentry           *dentry;
                struct nfs_fattr        *fattr;
        } clone_data;
#if IS_ENABLED(CONFIG_ENFS)
        void *enfs_option;      /* struct multipath_mount_options * */
#endif
};
```

## Where it lives

- File: `fs/nfs/internal.h`
- `nfs_client_initdata::enfs_option`: appended after
  `reconnect_timeout`, ~line 89 in the patched tree.
- `nfs_fs_context::enfs_option`: appended after the `clone_data`
  block, ~line 158.

## Why it's needed

The full propagation chain for a parsed `enfs_info=` mount option
is:

```mermaid
flowchart LR
    UM["mount.nfs -o enfs_info=..."] --> P["fs_context.c<br/>nfs_fs_context_parse_param()"]
    P -- "calls enfs_parse_mount_options()" --> Ctx["nfs_fs_context::enfs_option"]
    Ctx -- "patch 0019: nfs_init_server()" --> Init["nfs_client_initdata::enfs_option"]
    Init -- "patch 0019: nfs_create_rpc_client()" --> RC["rpc_create_args::multipath_option"]
    RC -- "rpc_create()" --> Clnt["rpc_clnt::multipath_option<br/>(set by adapter)"]
    Clnt --> CL["enfs.ko: multipath dispatch"]
```

This patch adds the two middle pointer slots. Without them, the
parsed options would have nowhere to live between
`fs_context.c` (where they're created) and `client.c` (where they're
consumed when the rpc client is built).

OpenEuler call sites that read these fields:

```c
/* vendor/openeuler/fs/nfs/fs_context.c:1599 */
ctx->enfs_option = NULL;

/* vendor/openeuler/fs/nfs/fs_context.c:1619 */
enfs_free_mount_options(ctx);

/* vendor/openeuler/fs/nfs/enfs/enfs_multipath_client.c:78 */
(struct multipath_mount_options *)(cl_init->enfs_option);

/* vendor/openeuler/fs/nfs/enfs_adapter.c:138 */
if (cl_init->enfs_option == NULL)
        return 0;
```

## Why this exact form

- **`void *`, not `struct multipath_mount_options *`.** The concrete
  type lives inside `enfs.ko` (`fs/nfs/enfs/enfs_multipath.h`).
  `fs/nfs/internal.h` is compiled into the in-tree `nfs.ko` build
  too, where `enfs.ko`'s headers are not available. Using a typed
  pointer would force a cross-module forward declaration. The void
  pointer is then cast on the consumer side
  (`enfs_multipath_client.c:78`).
- **Single comment indicating the real type.** `/* struct
  multipath_mount_options * */` is the only marker a reader has that
  the field is not actually opaque. Matches OE's convention.
- **Append, do not insert.** Both fields land at the end of their
  containing struct (or at the end of the relevant block in
  `nfs_fs_context`) so existing field offsets don't shift. Nothing
  in `nfs.ko` exports either struct as part of a stable ABI, but
  keeping offsets stable keeps debugger / crash-dump scripts
  pointing at the right slots after the patch.

## Observable effect for users

None directly. These are private struct slots inside the
mount-context bookkeeping. They are inert until patch 0011 starts
populating `ctx->enfs_option` from a parsed `enfs_info=` token, and
patch 0019 propagates it onward into the rpc client.

A `mount -t nfs -o enfs_info=...` (after the full stack lands) will
have a non-`NULL` `ctx->enfs_option` for the duration of the mount;
a stock `mount -t nfs ...` keeps both fields at `NULL` and behaves
identically to a non-enfs build.
