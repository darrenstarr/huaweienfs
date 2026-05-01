# 0007 — `include/linux/nfs_fs_sb.h`: add enfs fields, hidden from genksyms

Patch file: [`patches/ubuntu-7.0/0007-include-nfs_fs_sb-add-enfs-fields.patch`](../../patches/ubuntu-7.0/0007-include-nfs_fs_sb-add-enfs-fields.patch)

## What this change adds to stock Linux

Three additions to the public NFS-client header:

- `struct nfs_client::cl_multipath_data` — opaque `void *` slot that
  enfs uses to attach its per-`nfs_client` multipath state
  (`struct multipath_client_info`).
- `struct nfs_server::enfs_flags` — `int` bitfield holding
  `ENFS_SERVER_FLAG_*` bits (lookup-cache mode, "get capability in
  progress", etc.) per remote endpoint in a multipath set.
- Two `static inline` refcount helpers `nfsclient_refinc` /
  `nfsclient_refdec` — thin `refcount_inc` / `refcount_dec` wrappers
  used by enfs's DNS-resync path.

All three are wrapped in
`#if !defined(__GENKSYMS__) && IS_ENABLED(CONFIG_ENFS)` so genksyms
emits the **same** CRCs for `nfs.ko`'s exported symbols as a stock
Ubuntu kernel. Unmodified consumers (`nfs_acl`, `nfsd`, `lockd`,
`nfsv3.ko`, `nfsv4.ko`) load without `version magic` errors against
our patched `nfs.ko`.

**Before** (`include/linux/nfs_fs_sb.h`, around the tail of
`nfs_client`):

```c
        struct list_head        pending_cb_stateids;
        struct rcu_head         rcu;

#if IS_ENABLED(CONFIG_NFS_LOCALIO)
        struct timespec64       cl_nfssvc_boot;
        seqlock_t               cl_boot_lock;
```

**After**:

```c
        struct list_head        pending_cb_stateids;
        struct rcu_head         rcu;

#if !defined(__GENKSYMS__) && IS_ENABLED(CONFIG_ENFS)
        void                    *cl_multipath_data; /* hidden from genksyms */
#endif
#if IS_ENABLED(CONFIG_NFS_LOCALIO)
        struct timespec64       cl_nfssvc_boot;
        seqlock_t               cl_boot_lock;
```

For `nfs_server`:

```c
        bool                    has_sec_mnt_opts;
        struct kobject          kobj;
        struct rcu_head         rcu;
#if !defined(__GENKSYMS__) && IS_ENABLED(CONFIG_ENFS)
        int                     enfs_flags;     /* hidden from genksyms */
#endif
};
```

And, after the `NFS_CAP_*` defines:

```c
#if !defined(__GENKSYMS__) && IS_ENABLED(CONFIG_ENFS)
static inline void nfsclient_refinc(refcount_t *ref_count)
{
        refcount_inc(ref_count);
}

static inline void nfsclient_refdec(refcount_t *ref_count)
{
        refcount_dec(ref_count);
}
#endif
```

## Where it lives

- File: `include/linux/nfs_fs_sb.h`
- `cl_multipath_data`: in `struct nfs_client`, immediately after
  `rcu_head` and before the `CONFIG_NFS_LOCALIO` block (~line 130 in
  the patched tree).
- `enfs_flags`: at the very end of `struct nfs_server`, after `rcu`
  (~line 302).
- `nfsclient_refinc` / `nfsclient_refdec`: tail of the file, after
  the `NFS_CAP_MOVEABLE` define (~line 340).

## Why it's needed

### `cl_multipath_data`

This is the per-`nfs_client` hook for everything enfs hangs off a
mount. It is allocated and freed by the adapter's
`client_info_init` / `client_info_free` callbacks:

```c
/* vendor/openeuler/fs/nfs/enfs_adapter.c:143 */
ret = ops->client_info_init((void *)&client->cl_multipath_data, ...);

/* vendor/openeuler/fs/nfs/enfs_adapter.c:160 */
ops->client_info_free(clp->cl_multipath_data);
```

and read on the per-RPC and per-remount hot paths:

```c
/* vendor/openeuler/fs/nfs/enfs/dns_process.c:627 */
struct multipath_client_info *clp_info = clp->cl_multipath_data;

/* vendor/openeuler/fs/nfs/enfs/enfs_remount.c:137 */
nfs_client->cl_multipath_data;

/* vendor/openeuler/fs/nfs/enfs/enfs_lookup_cache.c:254 */
client_info = server->nfs_client->cl_multipath_data;
```

### `enfs_flags`

Per-server bitfield used to coordinate enfs's lookup-cache rewrites
and capability-probe state machine. The bits enfs sets/clears are:

- `ENFS_SERVER_FLAG_LOOKUP_CACHE_NOREG`
- `ENFS_SERVER_FLAG_LOOKUP_CACHE_NONE`
- `ENFS_SERVER_FLAG_GET_CAP_RUNNING`

Read sites:

```c
/* vendor/openeuler/fs/nfs/enfs/enfs_lookup_cache.c:250 */
if (server->enfs_flags & ENFS_SERVER_FLAG_GET_CAP_RUNNING)

/* vendor/openeuler/fs/nfs/enfs/enfs_lookup_cache.c:336 */
server->enfs_flags |= ENFS_SERVER_FLAG_GET_CAP_RUNNING;

/* vendor/openeuler/fs/nfs/enfs_adapter.c:272 */
return ((server->enfs_flags & flag) ? true : false);
```

### `nfsclient_refinc` / `nfsclient_refdec`

Used by the DNS-resync code path when it bumps an `nfs_client`
refcount across a workqueue boundary:

```c
/* vendor/openeuler/fs/nfs/enfs/dns_process.c:836 */
nfsclient_refinc(&clp->cl_count);
```

Defining them inline in this header avoids exporting another
`EXPORT_SYMBOL` from `nfs.ko`.

Without these three additions, `enfs.ko` fails to link
(`cl_multipath_data`, `enfs_flags` are accessed in over 15 sites; the
helpers in 1).

## Why this exact form

The KABI hiding trick (`#if !defined(__GENKSYMS__)`) is the load-
bearing detail. genksyms walks the preprocessed header to compute a
CRC for every exported symbol whose signature mentions one of these
structs (e.g. `nfs_alloc_server`, `nfs_create_server`). Add a field
inside the `genksyms` view of the struct and every dependent CRC
shifts — every other module built against the old CRC then fails
`module: disagrees about version of symbol nfs_create_server` at
load.

The OpenEuler tree had the same problem and resolved it the same way;
we kept the exact macro and field placement so an OE-side patch that
references `cl_multipath_data` or `enfs_flags` applies cleanly here.

The two refcount helpers are technically not needed by the genksyms
guard (`static inline` symbols don't appear in the CRC table), but we
wrap them anyway for symmetry — easier for a future reader to see at
a glance "everything in this enfs block is hidden from non-enfs
builds".

The two struct fields are appended **after** the original last field
in each struct so any stock caller that walks the structs by offset
keeps seeing the same offsets for the original fields. Only enfs-aware
code reaches the appended slots.

## Observable effect for users

None directly. These are all internal struct slots / inline helpers.

The downstream observable effects, once the rest of the stack is in
place:

- A multipath mount (`-o enfs_info=...`) populates
  `cl_multipath_data` with the parsed remote/local address lists.
- `enfs_flags` reflects per-server capability and lookup-cache state.
  enfs's `/proc` debug interfaces (`/proc/enfs/...`, added by patches
  not in this series) read these bits, but no `/proc` file is added
  by this patch alone.
- A non-enfs build (`CONFIG_ENFS=n`) compiles to byte-for-byte the
  same `nfs.ko` it would without this patch — both `struct` layouts
  and CRC tables are unchanged.
