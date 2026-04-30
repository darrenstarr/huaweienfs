# DKMS notes

## Why three modules

`dkms.conf.in` declares three `BUILT_MODULE_*` slots — `sunrpc`, `nfs`,
`enfs` — all installed into `/lib/modules/$KVER/updates/`. This works
because `depmod`'s default `/etc/depmod.d/ubuntu.conf` (and the upstream
default) ranks `updates/` higher than `kernel/`, so `modprobe sunrpc`
loads our patched copy instead of the in-tree one.

There is no coexistence between this package's modules and the stock
ones — `modprobe` only loads one `nfs.ko` (the highest-precedence one
in `modules.dep`), so installing this package wholesale replaces the
host's NFS client stack. Replacing in-tree modules from a DKMS package
has precedent (zfs-dkms, NVIDIA, mlnx-ofed). `apt purge enfs-dkms`
runs `dkms remove`, which deletes from `updates/` and lets `depmod`
fall back to `kernel/` — full rollback. Verify after every release.

Because of this, the user-facing rule is simple: while `enfs-dkms` is
installed, the kernel is using OpenEuler's NFS client behaviour. While
it isn't, the kernel is using stock Ubuntu's. There is no third state.

## Build-time inputs DKMS provides

Inside `MAKE[0]` we can use:

- `${kernel_source_dir}` — `/lib/modules/$kver/build`
- `${dkms_tree}` — typically `/var/lib/dkms`
- `${PACKAGE_NAME}` and `${PACKAGE_VERSION}` — from the same `dkms.conf`
- `${kernelver}`, `${arch}` — automatically set by DKMS

Our `MAKE[0]` invokes the kernel build system with `M=` pointing at
DKMS's staged source tree, which is exactly what an out-of-tree module
build expects.

## Why `REMAKE_INITRD="yes"`

Because we're replacing `nfs.ko` and `sunrpc.ko`, and root-on-NFS
systems pull both into the initramfs, an unrebuild initramfs after
`dkms install` would still load the stock modules at boot. `REMAKE_INITRD`
triggers `update-initramfs -u` for every kernel that gets the module.

## Why `BUILD_EXCLUSIVE_ARCH="x86_64 aarch64"`

Matches the OpenEuler `Kconfig` `depends on X86 || X86_64 || ARM64`. If
you decide to expand support, update both this line and the Kconfig
copy in `vendor/openeuler/fs/nfs/Kconfig`.

## Common operations

```bash
# Build + install for the running kernel:
sudo apt install dkms build-essential linux-headers-$(uname -r)
make dkms-install                # uses /var/lib/dkms by default

# Inspect:
dkms status -m enfs
modinfo /lib/modules/$(uname -r)/updates/enfs.ko

# Remove (restores stock nfs.ko/sunrpc.ko):
make dkms-uninstall

# Build a redistributable .deb instead:
make deb                         # writes ../enfs-dkms_<ver>_all.deb
```

## Failure modes to watch for

- **`modprobe: ERROR: could not insert 'enfs': Unknown symbol …`** —
  almost always means our `nfs.ko` or `sunrpc.ko` didn't end up loaded
  *first*, or the `updates/` precedence didn't take effect. Check
  `lsmod | grep -E 'sunrpc|nfs'` and `modinfo $(modinfo -n nfs)`.
- **kernel taint on load** — expected. enfs is GPL-licensed (so no
  proprietary taint) but the replacement of `nfs.ko`/`sunrpc.ko` will
  taint the kernel with `O` (out-of-tree). That's fine; just don't
  panic on `dmesg` warnings.
- **NFS root systems** — DKMS install on a root-on-NFS host will tear
  down NFS at the moment `modprobe -r nfs` runs (which DKMS does as
  part of the install). Only deploy on machines whose root FS is local.
