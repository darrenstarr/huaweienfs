# 0010 — `fs/nfs/super.c`: include enfs adapter, remount + mount hooks

Patch file: [`patches/ubuntu-7.0/0010-fs-nfs-super-add-enfs-hooks.patch`](../../patches/ubuntu-7.0/0010-fs-nfs-super-add-enfs-hooks.patch)

## What this change adds to stock Linux

Three additive hook sites in the NFS superblock layer, all guarded by
`CONFIG_ENFS`:

1. `#include "enfs_adapter.h"` alongside the other private headers,
   so this TU can call `enfs_free_mount_options()` and
   `enfs_trigger_get_server_capability()`.
2. In `nfs_reconfigure()` — the remount path — call
   `nfs_remount_iplist()` if the user supplied a new
   `enfs_info=` / `remoteaddrs=` / `localaddrs=` set. On failure,
   release the parsed options and bubble the error.
3. In `nfs_get_tree_common()` — the mount-completion path — kick off
   the enfs server-capability probe right after the superblock is
   marked active.

**Before** (`fs/nfs/super.c`, around the include block and
`nfs_reconfigure`):

```c
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#include "internal.h"
```

```c
        if (ctx->skip_reconfig_option_check)
                return 0;

        /*
         * noac is a special case. It implies -o sync, but that's not
         * necessarily reflected in the option string.
         */
```

**After**:

```c
#include "callback.h"
#include "delegation.h"
#include "iostat.h"
#if IS_ENABLED(CONFIG_ENFS)
#include "enfs_adapter.h"
#endif
#include "internal.h"
```

```c
        if (ctx->skip_reconfig_option_check)
                return 0;
#if IS_ENABLED(CONFIG_ENFS)
        if (ctx->enfs_option) {
                int error = nfs_remount_iplist(nfss->nfs_client, ctx->enfs_option);

                if (error) {
                        /* release remount option member */
                        enfs_free_mount_options(ctx);
                        return error;
                }
        }
#endif
```

And, in `nfs_get_tree_common()` after `s->s_flags |= SB_ACTIVE;`:

```c
        s->s_flags |= SB_ACTIVE;
#if IS_ENABLED(CONFIG_ENFS)
        if (server)
                enfs_trigger_get_server_capability(server);
#endif
        error = 0;
```

## Where it lives

- File: `fs/nfs/super.c`
- Include: ~line 67, between `iostat.h` and `internal.h`.
- Remount hook: in `nfs_reconfigure()`, ~line 1044, immediately after
  the `skip_reconfig_option_check` fast-exit.
- Mount-complete hook: in `nfs_get_tree_common()`, ~line 1352,
  immediately after `s->s_flags |= SB_ACTIVE`.

## Why it's needed

### Remount hook

The `enfs_info=` mount option is parsed at remount time the same way
as at first mount. If the new option set differs from the live one
(e.g. an admin added a second remote address to an existing mount),
enfs needs to push the new path list into its live multipath state
without unmounting:

```c
/* vendor/openeuler/fs/nfs/enfs/enfs_remount.c:174 */
int enfs_remount_iplist(struct nfs_client *nfs_client, void *enfs_option)
{
        ...
        /* walks the new remote/local lists, adds new xprts via
         * xprt_switch_add_xprt_locked, removes departed ones via
         * rpc_xprt_switch_remove_xprt */
}
```

If `nfs_remount_iplist` fails (ENOMEM, name-resolution failure, etc.)
the parsed options are released and the error is returned to the
caller — there is no half-applied state.

### Mount-complete hook

Once the superblock is active, enfs needs to probe each remote in the
multipath set for which NFSv3 ops it supports — specifically
`NFS3PROC_EXTEND`, the OE wire-format extension added in patch 0012.
The probe runs asynchronously on enfs's workqueue:

```c
/* vendor/openeuler/fs/nfs/enfs_adapter.c:276 */
void enfs_trigger_get_server_capability(struct nfs_server *server)
{
        ...
        ops->trigger_get_server_capability(server);
        ...
}
```

The probe is fire-and-forget; the result lands in `server->enfs_flags`
(the field added by patch 0007) and is consulted by enfs's
lookup-cache and failover logic.

### The include

Both `nfs_remount_iplist`, `enfs_free_mount_options`, and
`enfs_trigger_get_server_capability` are declared in
`fs/nfs/enfs_adapter.h`. The header lives next to `super.c` after
`scripts/build-src-tree.sh` drops it into `fs/nfs/`. Adding the
include is a precondition for the two hook sites to compile.

## Why this exact form

- **Remount hook fires before the noac/SYNC handling.** This matches
  the OE order. If the iplist update fails we want to bail before
  mutating any of the noac-derived flags, so the mount stays in a
  consistent pre-remount state.
- **Mount-complete hook is guarded on `if (server)`.** The stock code
  reaches the `out:` label with `server` possibly NULL (early-error
  paths). The OE patch had the same guard; we keep it.
- **Hook order matches OE byte-for-byte.** The lines we insert and
  their relative positions to surrounding stock code are identical
  to OE's. This is deliberate — the next time we re-sync from OE,
  these hooks should require zero rebase work.
- **Did not deviate from OE here.** Unlike patch 0019 (which adds a
  third hook OE only had two of) or patch 0006 (which rebases a flag
  bit), this patch is a verbatim port of OE's three changes.

## Observable effect for users

Combined with patches 0011 and 0019 (which is when the `enfs_info=`
option becomes acceptable end-to-end), an admin will see:

- `mount -o remount,enfs_info=...` actually re-applies the path list
  instead of being silently ignored, returning `EINVAL`/`ENOMEM`
  cleanly on bad input.
- Right after `mount -t nfs -o enfs_info=...` succeeds, enfs's
  workqueue starts firing capability probes. If the underlying
  servers don't speak the OE EXTEND op, `dmesg` may contain enfs
  log lines noting the fallback (depending on `enfs.ko`'s log
  verbosity).

This patch alone — with `enfs.ko` not loaded — has no observable
effect. The `ctx->enfs_option` test in `nfs_reconfigure()` is always
false (no patch sets it yet at this point in the series), and
`enfs_trigger_get_server_capability()` resolves to a no-op stub when
enfs is not registered.
