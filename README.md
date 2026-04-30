# enfs-dkms

A DKMS-packaged port of the OpenEuler **enfs** (Enhanced NFS) module to
the **Ubuntu 26.04 LTS** ("resolute") kernel (7.0.x).

`enfs` adds NFSv3/v4 client multipath, transport failover, round-robin
RPC dispatch and runtime path management on top of the in-tree Linux
NFS client. Upstream lives in `fs/nfs/enfs/` of the OpenEuler kernel
tree, branch `OLK-6.6`:

- Repository: <https://gitee.com/openeuler/kernel>
- Branch: <https://gitee.com/openeuler/kernel/tree/OLK-6.6>
- enfs source dir:
  <https://gitee.com/openeuler/kernel/tree/OLK-6.6/fs/nfs/enfs>
- Project Kconfig: see `vendor/openeuler/fs/nfs/Kconfig` (`config ENFS`)

The exact upstream commit we vendored is recorded in
`vendor/openeuler/UPSTREAM-REVISION`.

## Status

**Early scaffold.** OpenEuler sources are vendored under
`vendor/openeuler/`. The DKMS scaffolding, packaging and porting docs
are in place. End-to-end build against an Ubuntu 26.04 kernel is the
next milestone — see `docs/PORTING-NOTES.md`.

## What enfs does (one-page summary)

```mermaid
flowchart LR
    App["Application<br/>(read/write)"] --> NFSc["Linux NFS client<br/>(nfs.ko)"]
    NFSc --> Adapter["enfs_adapter<br/>(in nfs.ko)"]
    Adapter -. registers .-> Enfs["enfs.ko<br/>multipath / failover /<br/>round-robin / DNS"]
    Enfs --> SunRPC["sunrpc.ko<br/>+ sunrpc_enfs_adapter"]
    SunRPC -->|xprt 1| S1[("NFS server A")]
    SunRPC -->|xprt 2| S2[("NFS server B")]
    SunRPC -->|xprt 3| S3[("NFS server C")]
```

A single mount point uses several server addresses (or several local
source addresses) concurrently. RPCs round-robin across active
transports; if one transport stops responding, traffic moves to the
others. Server lists can be edited at runtime through `/proc`.

## Why DKMS

Stock Ubuntu kernels do not ship `enfs`, and the module is not a clean
out-of-tree drop-in: it requires patches inside `nfs.ko` and `sunrpc.ko`
as well. DKMS gives us:

- automatic rebuild against new Ubuntu kernel versions on `apt upgrade`,
- installation to `/lib/modules/$(uname -r)/updates/` (which `modprobe`
  prefers over `kernel/`), letting us *replace* the in-tree
  `nfs.ko` / `sunrpc.ko` without touching the kernel package, and
- a clean `dkms remove` rollback path that restores the stock modules.

## Layout

```
vendor/openeuler/         Verbatim OpenEuler OLK-6.6 sources we depend on
  fs/nfs/enfs/              the standalone enfs.ko sources
  fs/nfs/enfs_adapter.*     glue compiled into nfs.ko
  fs/nfs/{super,fs_context,nfs3xdr,internal.h,Kconfig,Makefile}
                            stock NFS files OpenEuler patches
  net/sunrpc/sunrpc_enfs_adapter.c
                            glue compiled into sunrpc.ko
  net/sunrpc/{clnt,xprt,Kconfig,Makefile}
                            stock SunRPC files OpenEuler patches
  include/linux/{nfs_fs_sb,nfs_xdr}.h
  include/linux/sunrpc/{sched,clnt,sunrpc_enfs_adapter}.h
  UPSTREAM-REVISION         pinned commit we vendored from
src/                      Project sources after porting (generated; gitignored)
compat/                   Kernel-version compat shims (Ubuntu 7.0 vs OE 6.6)
patches/                  Patches we apply on top of vendored sources
debian/                   dpkg packaging (enfs-dkms .deb)
scripts/                  Build / sync / VM-deploy helpers
docs/                     User docs + ARCHITECTURE / PORTING / DKMS notes
dkms.conf.in              DKMS manifest template
Kbuild                    Top-level out-of-tree build entry
Makefile                  `make help` lists targets
```

## Development environment (placeholders)

This project is developed against a libvirt host that hosts both the
kernel source workspace and the test VM. The public docs use these
placeholders; substitute your own values.

| Placeholder | What it is | This project's value |
|---|---|---|
| `<BUILD_HOST>` | ssh-reachable libvirt host | (see `secrets/beast.md` locally) |
| `<KERNEL_WORK_PATH>` | scratch dir on `<BUILD_HOST>` for kernel sources | (see `secrets/beast.md`) |
| `<TEST_VM_HOST>` | `user@ip` of the Ubuntu 26.04 test VM | (see `secrets/test-vm.md`) |
| `<VM_PATH>` | working dir on the test VM | `/home/<user>/enfs-dkms` |

If you `git clone` this repo, create a `secrets/` directory locally
(it's `.gitignore`d) and put your real values in it; or just override
on the command line:

```bash
make sync-vm  VM_HOST=ubuntu@10.0.0.42  VM_PATH=/home/ubuntu/enfs-dkms
```

The reference deployment for this project is documented in
`secrets/beast.md` and `secrets/test-vm.md` (local-only).

## Quick start

```bash
make help                # list all targets and current variable values
make port                # materialise src/ from vendor + compat + patches
make sync-vm             # rsync to the test VM (set VM_HOST first)
make build-on-vm         # build modules on the test VM against its headers
make smoke-on-vm         # build + dkms-install + modprobe enfs + dmesg tail
```

## Documentation map

- **Architecture** — `docs/ARCHITECTURE.md` (component map, hook sites)
- **Porting status** — `docs/PORTING-NOTES.md` (API drift table, work list)
- **DKMS internals** — `docs/DKMS-NOTES.md` (why three modules, install layout)
- **User docs** — `docs/user/` (planned: install, mount syntax, ops, troubleshoot)
- **Stock-vs-eNFS diff report** — `docs/differences/` (planned)
- **AI agent guidance** — `docs/ai-agent-guide.md` (rules for Claude/etc.)

## License

GPL-2.0-only.

Vendored OpenEuler files retain their original SPDX/copyright headers
verbatim — see each file. Files genuinely authored by Huawei carry an
explicit `Copyright (c) Huawei Technologies` line; modified Linux
kernel files retain their original kernel-author copyrights (Rick
Sladkey, David Howells, Olaf Kirch, the SunRPC contributors, etc.).
The `debian/copyright` file enumerates this with file-glob stanzas.
