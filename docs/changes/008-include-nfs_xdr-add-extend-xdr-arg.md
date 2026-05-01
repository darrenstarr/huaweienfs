# 0008 — `include/linux/nfs_xdr.h`: add `struct nfs_extend_xdr_arg`

Patch file: [`patches/ubuntu-7.0/0008-include-nfs_xdr-add-extend-xdr-arg.patch`](../../patches/ubuntu-7.0/0008-include-nfs_xdr-add-extend-xdr-arg.patch)

> **Compat alert.** This struct is one half of a non-standard NFSv3
> wire-format extension (the OpenEuler "EXTEND" op, `NFS3PROC_EXTEND
> = 22`). Standard NFSv3 servers — anything not running an
> OE-derived nfs-server — will reject calls that carry this payload.
> See `docs/PORTING-NOTES.md` § "nfs3xdr v3 wire format" and the
> matching XDR encode/decode in patch 0012.

## What this change adds to stock Linux

A small XDR carrier struct exposed via a public header so that both
`nfs.ko` (the encode/decode pair added in patch 0012) and `enfs.ko`
(`fs/nfs/enfs/exten_call.c`, which fills the buffer) can speak the
same struct layout.

**Before** (`include/linux/nfs_xdr.h`, after `struct nfs_rpc_ops`):

```c
struct nfs_rpc_ops {
        ...
        void    (*enable_swap)(struct inode *inode);
        void    (*disable_swap)(struct inode *inode);
};

/*
 * Helper functions used by NFS client and/or server
 */
```

**After**:

```c
struct nfs_rpc_ops {
        ...
        void    (*enable_swap)(struct inode *inode);
        void    (*disable_swap)(struct inode *inode);
};

#if IS_ENABLED(CONFIG_ENFS)
struct nfs_extend_xdr_arg {
        int      maxsize;
        int      buflen;
        char    *pBuf;
};
#endif

/*
 * Helper functions used by NFS client and/or server
 */
```

## Where it lives

- File: `include/linux/nfs_xdr.h`
- Insertion point: immediately after `struct nfs_rpc_ops` (around line
  1860 in the patched tree, just before the helper-function block).

## Why it's needed

The struct is the input to both halves of `nfs3xdr.c`'s extended-call
support (added in patch 0012):

- `nfs3_xdr_enc_extend3args()` reads `pBuf` / `buflen` to encode the
  outgoing opaque payload.
- `nfs3_xdr_dec_extend3res()` writes the decoded server response back
  into `pBuf`, bounds-checking against `maxsize`.

The caller — enfs's path-state subsystem — populates the struct
in two sites:

```c
/* vendor/openeuler/fs/nfs/enfs/exten_call.c:606 */
struct nfs_extend_xdr_arg xdr_arg = { 0 };

/* vendor/openeuler/fs/nfs/enfs/exten_call.c:643 */
struct nfs_extend_xdr_arg xdr_arg = { 0 };
```

These calls dispatch the EXTEND RPC against a specific xprt to ask
the server "do you support this op? what is your path-state?" — the
result feeds enfs's failover decision tree.

Putting the struct in a public header (rather than `fs/nfs/internal.h`)
is required because `enfs.ko` is built outside `fs/nfs/` and cannot
include the private headers without going through symlink contortions
in `scripts/build-src-tree.sh`.

## Why this exact form

The struct is a **byte-for-byte port** of OE's
`vendor/openeuler/include/linux/nfs_xdr.h:1729`. Field names,
including the camelCase `pBuf` and the int-typed `maxsize`/`buflen`
(rather than the more idiomatic `size_t`), are preserved so that any
future re-sync from OE applies cleanly.

The wrapping `#if IS_ENABLED(CONFIG_ENFS)` ensures a non-enfs build
sees no diff at all in this file. (genksyms is not at risk here
because no exported function signature mentions
`struct nfs_extend_xdr_arg`.)

## Observable effect for users

None on its own. The struct is inert until patch 0012 wires the
encode/decode pair into the `nfs3_procedures[]` table and `enfs.ko`
loads.

Once the full stack is in place, the user-visible effect is:

- enfs sends NFS3PROC_EXTEND (op 22) RPCs to each transport in a
  multipath set as part of capability discovery.
- An OE-style server replies with its path-state payload; enfs uses
  this for failover decisions.
- A standard NFSv3 server replies with `NFS3ERR_NOTSUPP` (or drops
  the call); enfs treats that as "this server doesn't speak the
  extension" and falls back to its conservative path-state policy.

No new `/proc` file, mount option, or dmesg line is added by this
patch alone.
