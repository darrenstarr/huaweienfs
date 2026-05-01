# 0012 — `fs/nfs/nfs3xdr.c`: NFSv3 EXTEND op + XDR encode/decode

Patch file: [`patches/ubuntu-7.0/0012-fs-nfs-nfs3xdr-extend-call.patch`](../../patches/ubuntu-7.0/0012-fs-nfs-nfs3xdr-extend-call.patch)

> **Compat alert.** This patch wires a non-standard NFSv3 op
> (`NFS3PROC_EXTEND = 22`) into the procedure table. Standard NFSv3
> servers do not implement it and will reply with `NFS3ERR_NOTSUPP`
> (or drop the call). The wire-format extension itself is OE-only.
> See `docs/PORTING-NOTES.md` § "nfs3xdr v3 wire format".

## What this change adds to stock Linux

End-to-end support for OE's "extended call" NFSv3 op. Five additions:

1. Header includes / defines: pull in `struct nfs_extend_xdr_arg`
   from patch 0008 via `<linux/nfs_xdr.h>`. Define
   `EXTEND_CMD_MAX_BUF_LEN` (819200 = 800 KiB), and define
   `NFS3PROC_EXTEND = 22` inline if the UAPI header doesn't ship it
   (which is the case on stock Ubuntu 7.0 — patching uapi is
   undesirable).
2. Two size macros — `NFS3_extendargs_sz` and `NFS3_extendres_sz` —
   so the `PROC()` macro expands to the right `p_arglen` /
   `p_replen` for the new op.
3. `nfs3_xdr_enc_extend3args()` — encode the outgoing opaque payload
   as a length-prefixed byte array. enfs.ko populates the buffer in
   `fs/nfs/enfs/exten_call.c`.
4. `nfs3_xdr_dec_extend3res()` — decode the response: NFS3 status
   word, then a length-prefixed opaque payload, bounds-checked
   against the caller-supplied `decArg->maxsize`.
5. `PROC(EXTEND, extend, extend, 0)` slot in the
   `nfs3_procedures[]` table at index `NFS3PROC_EXTEND`.

All under `CONFIG_ENFS`.

**The encoder** (the one that talks to the wire):

```c
#if IS_ENABLED(CONFIG_ENFS)
static void nfs3_xdr_enc_extend3args(struct rpc_rqst *req,
        struct xdr_stream *xdr, const void *data)
{
        const struct nfs_extend_xdr_arg *encArg = data;
        __be32 *p;

        WARN_ON_ONCE(encArg->buflen > EXTEND_CMD_MAX_BUF_LEN);
        p = xdr_reserve_space(xdr, 4 + encArg->buflen);
        xdr_encode_opaque(p, encArg->pBuf, encArg->buflen);
}
```

**The decoder**:

```c
static int nfs3_xdr_dec_extend3res(struct rpc_rqst *req,
        struct xdr_stream *xdr, void *result)
{
        enum nfs_stat status;
        int error;
        struct nfs_extend_xdr_arg *decArg = result;
        int length;
        __be32 *p;

        error = decode_nfsstat3(xdr, &status);
        if (unlikely(error))
                goto out;
        if (status != NFS3_OK)
                goto out_default;

        memset(decArg->pBuf, '\0', decArg->maxsize);
        p = xdr_inline_decode(xdr, 4);
        if (unlikely(!p))
                return -EIO;
        length = be32_to_cpup(p++);
        if (unlikely(length > decArg->maxsize)) {
                dprintk("NFS: extend response (%u) > max_size (%d)\n",
                        length, decArg->maxsize);
                return -E2BIG;
        }
        ...
}
```

**The procedure-table slot**:

```c
        PROC(COMMIT,            commit,         commit,         5),
#if IS_ENABLED(CONFIG_ENFS)
        PROC(EXTEND,            extend,         extend,         0),
#endif
};
```

## Where it lives

- File: `fs/nfs/nfs3xdr.c`
- Includes / defines: ~line 28.
- Argsize macros: ~line 74 (`NFS3_extendargs_sz`) and ~line 94
  (`NFS3_extendres_sz`).
- `nfs3_xdr_enc_extend3args()` / `nfs3_xdr_dec_extend3res()`: ~line
  2462, immediately before the `nfs3_procedures[]` table.
- `PROC(EXTEND, ...)`: ~line 2537, last entry of `nfs3_procedures[]`.

## Why it's needed

enfs's path-state subsystem fires this op against every transport in
a multipath set to ask "do you speak the OE extension? what is your
current path-state?":

```c
/* vendor/openeuler/fs/nfs/enfs/exten_call.c:609 */
.rpc_proc = &nfs3_procedures[NFS3PROC_EXTEND],

/* vendor/openeuler/fs/nfs/enfs/exten_call.c:646 */
.rpc_proc = &nfs3_procedures[NFS3PROC_EXTEND],
```

For this dispatch to work, `nfs3_procedures[NFS3PROC_EXTEND]` must
exist — which is exactly what this patch provides. The procedure
table is consumed via the encoder/decoder pair the SunRPC layer
calls during `rpc_call_sync` / `rpc_call_async`.

The 800 KiB buffer ceiling is sized to comfortably hold OE's
path-state response payload (which is small — a few KiB in normal
operation) with headroom for future protocol additions, while
staying well below the SunRPC slab allocation cliff.

## Why this exact form

- **Inline `#define NFS3PROC_EXTEND` if not already defined.** The
  proper home for this constant is `include/uapi/linux/nfs3.h`, but
  patching uapi headers is strictly avoided in this DKMS package
  (uapi changes ripple into every userspace consumer). The local
  `#ifndef` guard means: if Ubuntu ever adds `NFS3PROC_EXTEND` to
  uapi, our local define silently steps aside.
- **Camel-case field names (`pBuf`, `decArg`).** Carried over
  verbatim from OE's
  `vendor/openeuler/fs/nfs/nfs3xdr.c:1385` so future re-syncs apply
  cleanly. Out of step with kernel style, but keeping it identical
  is more important than fixing it here.
- **Bounds-check returns `-E2BIG` not `-EOVERFLOW`.** OE's choice;
  we keep it. `-E2BIG` is what the upstream NFSv4 XDR decoders use
  for analogous overruns.
- **`memset(decArg->pBuf, '\0', decArg->maxsize)` before decoding.**
  Defensive — caller may have left stale bytes in the buffer. OE
  does this; we match.
- **`PROC(EXTEND, extend, extend, 0)` last in the table.** The
  numeric index `NFS3PROC_EXTEND = 22` matches its array position
  (entries 0 through 21 are the standard ops). The trailing `0` is
  the per-op cache shift (no caching for this op — it's a probe).

## Observable effect for users

No `/proc`, sysfs, or mount-option surface. The user-visible effects
are entirely indirect, mediated by `enfs.ko`:

- An admin who runs `tcpdump -i <iface> -nn 'port 2049'` during a
  multipath mount will see NFSv3 RPCs with procedure number 22 going
  out shortly after the mount completes (these are enfs's capability
  probes).
- Servers that don't speak the extension reply with
  `NFS3ERR_NOTSUPP`. enfs treats that as "this server is
  multipath-incapable" and skips it from path-state polling.
- The patch on its own — without `enfs.ko` loaded and without
  `enfs_trigger_get_server_capability()` having ever been called —
  contributes one extra (unused) entry to the in-memory
  `nfs3_procedures[]` table and nothing else. Memory cost: one
  `struct rpc_procinfo` (~48 bytes).

The patch is also a precondition for patch 0018, which exports
`nfs3_procedures` so that `enfs.ko` (which builds outside `nfs.ko`)
can index into it.
