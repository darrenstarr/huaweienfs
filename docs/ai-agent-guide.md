# AI agent guide (Claude / Cursor / Aider / Copilot / etc.)

This file is the public-safe equivalent of a `CLAUDE.md` (which is
gitignored in this repo). If you're an AI coding agent assisting on
this codebase, read this top-to-bottom first.

## Top priority: this is a public repo

The git remote points at a public GitHub repository. **Treat every
edit as if it will be force-pushed in the next minute.** That means:

- Never write real hostnames, IPs, MAC addresses, usernames or
  passwords into any file outside `secrets/` (which is `.gitignore`d).
- The placeholders to use in public files are listed in `README.md` →
  *Development environment*. Use them.
- If you accidentally type a real value, fix it before the next
  `git add`.
- Before suggesting a `git commit` or `git push`, run
  `git diff --cached` and re-grep for IPs, MACs and usernames:
  `git diff --cached | grep -nE '10\.[0-9]+\.[0-9]+\.[0-9]+|[0-9a-f]{2}(:[0-9a-f]{2}){5}|password'`.

## What this project is

A DKMS-packaged port of the OpenEuler **enfs** module to Ubuntu 26.04
LTS. Read `README.md` and then `docs/ARCHITECTURE.md` before making
non-trivial edits — particularly the part about the **3-module
replacement** (sunrpc.ko + nfs.ko + enfs.ko, all installed into
`/lib/modules/$KVER/updates/`). Anything that implies shipping
`enfs.ko` standalone is wrong.

## Source-of-truth files

- `vendor/openeuler/` — verbatim OpenEuler OLK-6.6 sources, pinned to
  the commit in `vendor/openeuler/UPSTREAM-REVISION`. **Never edit
  by hand.** If sources need refreshing, run
  `scripts/sync-from-openeuler.sh`.
- `compat/enfs_compat.h` — the *only* place where kernel-version
  drift shims live. Each `#if LINUX_VERSION_CODE` block must
  document what it is papering over.
- `patches/series` — quilt-style patches applied on top of `vendor/`
  during `make port` for changes too invasive for `compat/` alone.
- `src/` — **generated** by `make port`; gitignored. Never edit
  directly.

## Commands

```bash
make help                # list all targets and current variable values
make port                # materialise src/ from vendor + compat + patches
make modules             # out-of-tree kernel build (after `make port`)
make clean
make dkms-conf           # generate dkms.conf from dkms.conf.in
make dkms-install        # stage to /usr/src/enfs-VER, then dkms add/build/install
make dkms-uninstall      # restores stock nfs.ko/sunrpc.ko
make deb                 # dpkg-buildpackage -us -uc -b
make sync-vm             # rsync project (minus vendor/openeuler) to test VM
make build-on-vm         # sync + ssh + make modules on the VM
make smoke-on-vm         # sync + dkms-install + modprobe enfs + dmesg tail
```

`VM_HOST` and `VM_PATH` must be set on the command line, in the
shell, or in `secrets/local-env.sh` (the make target `_check-vm-host`
will refuse to run otherwise). See `secrets/README.md`.

## When porting

1. Always rerun `make port` after editing `compat/`, `patches/`, or
   `vendor/`.
2. Surface real API drift by running `make build-on-vm`. Each finding
   becomes a row in `docs/PORTING-NOTES.md` and (usually) a
   `#if LINUX_VERSION_CODE` block in `compat/enfs_compat.h`.
3. Keep `vendor/openeuler/` byte-identical to upstream. Diffs against
   upstream live in `compat/` or `patches/`, never in `vendor/`.

## When changing the build

If you need to add a new vendored OpenEuler file, also add its path to
the `PATHS` array in `scripts/sync-from-openeuler.sh` so future
refreshes pick it up.

If you change the set of kernel modules produced (e.g., split off a
helper `.ko`), update **both** the top-level `Kbuild` *and* the
`BUILT_MODULE_*` slots in `dkms.conf.in`.

## Testing assumes a non-NFS-root host

Installing this DKMS package replaces the running kernel's `nfs.ko`
and `sunrpc.ko`. On a host whose root filesystem is on NFS, the
`modprobe -r nfs` step that DKMS runs during install will rip the
root FS out from under the system. Only test on local-disk hosts (the
project's reference test VM qualifies — its root is on a virtio-blk
qcow2).

## Don't do these things

- Don't commit `dkms.conf` (it's generated).
- Don't commit anything from `src/` (it's generated).
- Don't add the OpenEuler kernel git history into this repo. We
  vendor only the files we depend on, pinned by SHA.
- Don't change SPDX headers in vendored files.
- Don't blanket-stamp Huawei copyright on stock-Linux files (the
  vendored `super.c`, `clnt.c`, etc. retain their original kernel
  authorship). See `debian/copyright`.
