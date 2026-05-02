# 0011 — `fs/nfs/fs_context.c`: accept `enfs_info=` / `remoteaddrs=` / `localaddrs=` mount options

Patch file: [`patches/ubuntu-7.0/0011-fs-nfs-fs_context-add-enfs_info-mount-option.patch`](../../patches/ubuntu-7.0/0011-fs-nfs-fs_context-add-enfs_info-mount-option.patch)

## What this change adds to stock Linux

This is the **largest** patch in the series — seven hook sites that
together teach the NFS fs_context parser to accept the enfs multipath
mount syntax. All guarded by `CONFIG_ENFS`.

The user-visible effect is a new mount-option vocabulary:

```bash
mount -t nfs -o enfs_info=...,remoteaddrs=10.0.0.1~10.0.0.2,localaddrs=10.0.1.1 ...
mount -t nfs -o slookupcache=...,alookupcache=... ...
```

The seven sites:

1. `#include "enfs_adapter.h"`.
2. Five new `Opt_*` enum values — `Opt_remote_addrs`, `Opt_local_iplist`,
   `Opt_enfs_info`, `Opt_slookupcache`, `Opt_alookupcache`.
3. Five matching `fsparam_string` entries in `nfs_fs_parameters[]` so
   the generic mount-option parser recognises the new tokens.
4. A small helper `getNfsMultiPathOpt()` translating an `Opt_*` value
   to the `enum nfsmultipathoptions` enfs's parser uses
   (`REMOTEADDR` / `LOCALADDR` / `INVALID_OPTION`).
5. Switch-case handlers in `nfs_fs_context_parse_param()`:
   - `remote_addrs` / `local_iplist` → call
     `enfs_parse_mount_options()` and translate `-ENOMEM` / `-ENOSPC`
     / `-EINVAL` / `-EOPNOTSUPP` into the right error label.
   - `enfs_info` / `slookupcache` / `alookupcache` → accepted but
     deferred (no-op here; enfs handles them later).
   - Two new error labels (`out_limit`, `out_nomem`) for the parser
     return paths.
6. In `nfs_validate_text_mount_data()` — after `nfs_parse_source()`
   has populated `ctx->nfs_server.hostname` — call
   `nfs_multipath_set_mount_data()` so enfs sees the resolved
   hostname before `nfs_mod` loading begins.
7. Lifecycle:
   - fs_context constructor: initialise `ctx->enfs_option = NULL`.
   - fs_context destructor: call `enfs_free_mount_options(ctx)` before
     freeing `ctx`.

**Before** (snippet of `nfs_fs_context_parse_param`):

```c
        case Opt_sloppy:
                ctx->sloppy = true;
                break;
        }

        return 0;
out_invalid_value:
        ...
```

**After**:

```c
        case Opt_sloppy:
                ctx->sloppy = true;
                break;
#if IS_ENABLED(CONFIG_ENFS)
        case Opt_local_iplist:
        case Opt_remote_addrs:
                switch (enfs_parse_mount_options(getNfsMultiPathOpt(opt),
                                                 param->string, ctx, fc)) {
                case 0:
                        break;
                case -ENOMEM:
                        goto out_nomem;
                case -ENOSPC:
                        goto out_limit;
                case -EINVAL:
                        goto out_invalid_address;
                case -EOPNOTSUPP:
                        goto out_invalid_address;
                }
                break;
        case Opt_enfs_info:
        case Opt_slookupcache:
        case Opt_alookupcache:
                break;
#endif
        }

        return 0;
out_invalid_value:
        ...
out_bad_transport:
        return nfs_invalf(fc, "NFS: Unrecognized transport protocol");
#if IS_ENABLED(CONFIG_ENFS)
out_limit:
        return nfs_invalf(fc, "NFS: param is more than supported limit");
out_nomem:
        return nfs_invalf(fc, "NFS: not enough memory to parse option");
#endif
```

## Where it lives

- File: `fs/nfs/fs_context.c`
- Include: ~line 27.
- Opt_* enum extension: ~line 104.
- `nfs_fs_parameters[]`: ~line 238.
- `getNfsMultiPathOpt()`: ~line 513.
- Switch-case in `nfs_fs_context_parse_param()`: ~line 1074.
- Error labels: ~line 1108.
- `nfs_multipath_set_mount_data()` call in
  `nfs_validate_text_mount_data()`: ~line 1630.
- `ctx->enfs_option = NULL` in fs_context constructor: ~line 1716.
- `enfs_free_mount_options(ctx)` in destructor: ~line 1737.

## Why it's needed

Mount-option parsing is the entry point for the entire enfs feature.
Without these hooks the kernel's generic fs_context parser rejects
`enfs_info=` etc. with `Bad value for 'enfs_info'` and the mount
fails before any enfs code runs.

The two real workhorses called here are:

- `enfs_parse_mount_options()` — parses the comma/tilde-separated
  IP-address syntax (`10.0.0.1~10.0.0.2~10.0.0.3`) and stashes the
  result in `ctx->enfs_option` (the field added by patch 0009):

  ```c
  /* vendor/openeuler/fs/nfs/enfs_adapter.c:80 */
  int enfs_parse_mount_options(enum nfsmultipathoptions option, char *str,
                               struct nfs_fs_context *ctx, struct fs_context *fc)
  ```

- `nfs_multipath_set_mount_data()` — given the resolved hostname
  (after nfs's own source parser ran), tells enfs which "primary"
  server this multipath set hangs off:

  ```c
  /* vendor/openeuler/fs/nfs/enfs_adapter.c:244 */
  void nfs_multipath_set_mount_data(void **opt, const char *hostname)
  ```

The five `Opt_*` tokens cover the full OE-style enfs syntax:

- `remoteaddrs=` / `localaddrs=` — the actual IP-address lists.
- `enfs_info=` — historically held a JSON-ish blob; in the current
  OE source it is accepted but ignored at parse time and consumed
  later from the live state. We preserve the token so existing
  mount lines from OE keep working.
- `slookupcache=` / `alookupcache=` — historical
  symbolic/attribute lookup-cache mode hints; same situation.

## Why this exact form

- **Three of the five new tokens are accepted but not acted on
  here.** OE chose to filter them in `enfs_init.c` and the multipath
  client constructor instead, leaving fs_context.c as a pure
  forwarder. We follow the same split: cleaner backports.
- **`getNfsMultiPathOpt()` rather than passing the raw `Opt_*`
  value.** enfs's parser has its own enum (`enum nfsmultipathoptions`)
  defined in `enfs_adapter.h`. The mapping function decouples the
  two enums so future additions to either don't force a coordinated
  change in the other.
- **Error label split.** Stock fs_context uses a single
  `out_invalid_address` label for malformed addresses; we add two
  more (`out_limit`, `out_nomem`) so a malformed-syntax error and
  an out-of-memory error give different messages to the user. Both
  labels are cheap (one line each).
- **`enfs_free_mount_options(ctx)` in the destructor handles a NULL
  `ctx->enfs_option`** — checked in the adapter, so we don't need
  a guard at the call site. Matches OE.

## Observable effect for users

This is the patch that makes the enfs mount syntax usable. After it
lands (combined with patches 0010, 0019, and a loaded `enfs.ko`):

- `mount -t nfs -o vers=3,enfs_info=...,remoteaddrs=A~B~C,localaddrs=L1~L2 server:/exp /mnt`
  succeeds and produces a multipath NFS mount across `A`, `B`, `C`.
- Bad address syntax produces
  `NFS: Failed to parse: Invalid IP address`,
  `NFS: param is more than supported limit`, or
  `NFS: not enough memory to parse option`, depending on the failure
  mode.
- A stock `mount -t nfs ...` (no enfs options) is unaffected: the
  new switch-case branches are not reached, `ctx->enfs_option` stays
  NULL, and the destructor's `enfs_free_mount_options(ctx)` is a
  no-op when the field is NULL.

No new `/proc` file or sysfs node is added by this patch. The mount
behaviour itself is the user-facing surface.
