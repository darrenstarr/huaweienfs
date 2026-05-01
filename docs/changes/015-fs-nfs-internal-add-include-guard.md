# 0015 — `fs/nfs/internal.h`: add include guard

Patch file: [`patches/ubuntu-7.0/0015-fs-nfs-internal-add-include-guard.patch`](../../patches/ubuntu-7.0/0015-fs-nfs-internal-add-include-guard.patch)

## What this change adds to stock Linux

A standard `#ifndef` / `#define` / `#endif` include guard around
`fs/nfs/internal.h`. Stock has none — fine when each translation
unit includes the header exactly once.

**Before** (`fs/nfs/internal.h`, top of file):

```c
/*
 * NFS internal definitions
 */

#include "nfs4_fs.h"
#include <linux/fs_context.h>
#include <linux/security.h>
```

**After**:

```c
/*
 * NFS internal definitions
 */

#ifndef _ENFS_NFS_INTERNAL_H_GUARD_
#define _ENFS_NFS_INTERNAL_H_GUARD_


#include "nfs4_fs.h"
#include <linux/fs_context.h>
#include <linux/security.h>
```

…and matching `#endif` at the very end of file.

## Where it lives

- File: `fs/nfs/internal.h`
- `#ifndef` / `#define`: top of file, after the comment block, before
  the first `#include` (lines 6-9 in the patched tree).
- `#endif`: tail of file, after the last `NFS_ODIRECT_*` define
  (~line 1010).

## Why it's needed

Patch 0010 makes `fs/nfs/super.c` `#include "enfs_adapter.h"` *and*
`#include "internal.h"`. `enfs_adapter.h` itself transitively
includes `internal.h` (it needs the `nfs_fs_context` and
`nfs_client_initdata` definitions touched by patches 0009 and 0011).

Without a guard, the second `#include "internal.h"` in `super.c`
re-emits every type, struct, enum, and `static inline` in the file
into the same translation unit. The compiler then errors out with
~30 distinct `redefinition` diagnostics — every struct, every
enum, every static-inline helper.

The same problem applies to `fs/nfs/client.c` after patch 0019, and
to `fs/nfs/fs_context.c` after patch 0011. All three TUs include
`enfs_adapter.h` alongside `internal.h`.

## Why this exact form

- **`_ENFS_NFS_INTERNAL_H_GUARD_` macro name.** Deliberately
  enfs-prefixed and double-underscore-bracketed to avoid colliding
  with anything stock might add later. Mainline upstream might one
  day add its own guard with the conventional `_NFS_INTERNAL_H` /
  `_FS_NFS_INTERNAL_H` macro; ours is far enough away that both
  could coexist.
- **Two blank lines after `#define`.** Cosmetic — separates the
  guard from the existing include block visually.
- **No `#pragma once`.** Kernel-style; `#ifndef` guards are the
  convention.
- **Why is this not in patch 0009 or 0011?** It is logically
  separate from any of the field additions; bundling it would have
  conflated "add field" diffs with "wrap whole file" diffs in the
  per-patch review. Splitting it out keeps each patch single-
  purpose.

## Observable effect for users

None. This is a pure compile-time safety net. Stock kernel builds
(`CONFIG_ENFS=n` and `CONFIG_ENFS=m`) produce identical object
files for any TU that includes `internal.h` exactly once — which is
all of them in stock and all of them in our enfs build that don't
also include `enfs_adapter.h`.

The patch is precondition-only: without it, three other patches in
this series would not compile.
