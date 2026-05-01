# Uninstall and Rollback

Removing enfs returns the host to a stock Ubuntu NFS client. The DKMS
package is designed for clean rollback: the patched modules go away,
the in-tree `nfs.ko` / `sunrpc.ko` / friends become primary again, and
no kernel package change is needed.

This page is the recipe and the verification.

## Before you start

- Unmount any enfs-managed mounts:

  ```bash
  mount | grep 'type nfs ' | awk '{print $3}' | xargs -r sudo umount
  ```

- Confirm nothing critical depends on the host's NFS client right now
  — the rollback briefly takes `sunrpc`, `nfs` and friends out of
  service while the modules swap.

## Path A — `dkms remove` (source install)

If you installed via `make dkms-install` from the repo:

```bash
sudo dkms remove -m enfs --all
sudo depmod -a
```

`dkms remove --all` deletes the modules from
`/lib/modules/$(uname -r)/updates/dkms/` for every kernel where they
were installed, removes the registration from `/var/lib/dkms/enfs/`,
and (with the `depmod` step) refreshes the module index so the kernel
sees the in-tree `nfs.ko` / `sunrpc.ko` again.

## Path B — `apt purge` (.deb install, when packaged)

If you installed via `apt install enfs-dkms`:

```bash
sudo apt purge enfs-dkms
```

`apt purge` triggers the same `dkms remove --all` underneath, plus
removes the `/usr/src/enfs-<VER>/` source tree and any dpkg-managed
config files.

## Manual cleanup (both paths)

The DKMS package does **not** own the support files you may have
created during installation; remove them by hand if you no longer
want the host to have these tweaks.

### The `nfsd` blacklist

If you created `/etc/modprobe.d/zz-no-nfsd.conf` per
[02-installation.md](02-installation.md) and you now want this host to
be able to serve NFS again:

```bash
sudo rm /etc/modprobe.d/zz-no-nfsd.conf
sudo update-initramfs -u
```

You will need a reboot for `nfsd` to load on its own again. If you
never plan to serve NFS from this host, you can leave the blacklist
in place — it is harmless without enfs installed.

### The `secrets/` setup directory

If you cloned the repo and created `secrets/` for your build host
config (it is `.gitignore`d, never reaches git), and you no longer
need that workspace:

```bash
rm -rf <PATH_TO_CLONED_REPO>/secrets/
```

Reminder: `secrets/` may have contained ssh hosts, kernel work paths,
and the test-VM address. Treat it like any other credentials
directory when disposing of it.

### The cloned repo

If the source-install workspace itself is no longer needed:

```bash
rm -rf <PATH_TO_CLONED_REPO>
```

There is nothing on the host outside `/lib/modules/`, `/var/lib/dkms/`
and (optionally) `/etc/modprobe.d/zz-no-nfsd.conf` that the package
ever wrote. Once those are cleaned up, the host is in the same state
it was before you ever installed enfs.

## Verifying the rollback

The single most important check is **which `nfs.ko` does the kernel
think it should load now?**

```bash
modinfo -n nfs
```

Before rollback (enfs active):

```
/lib/modules/7.0.0-15-generic/updates/dkms/nfs.ko
```

After rollback (stock active):

```
/lib/modules/7.0.0-15-generic/kernel/fs/nfs/nfs.ko
```

The path must be under `kernel/`, not `updates/dkms/`. Same check
for the others:

```bash
for m in sunrpc nfs nfsv3 nfs_acl lockd; do
    printf '%-10s %s\n' "$m" "$(modinfo -n $m)"
done
```

DKMS should no longer have a record of `enfs`:

```bash
dkms status -m enfs
# (no output)
```

`/proc/enfs/` should not exist (the directory only appears once
`enfs.ko` is loaded):

```bash
ls /proc/enfs/ 2>&1
# ls: cannot access '/proc/enfs/': No such file or directory
```

A vanilla single-path NFS mount should still work:

```bash
sudo mount -t nfs -o vers=3,nolock <NFS_SERVER_1>:/<EXPORT> <MOUNT_POINT>
mount | grep <MOUNT_POINT>
sudo umount <MOUNT_POINT>
```

If all of the above pass, the rollback is complete and the host is
back to stock.

## Reinstalling later

The `secrets/` directory is the only piece of state that does not
auto-rebuild. The DKMS package itself can be re-added at any time by
re-running `make dkms-install` from a fresh clone, or
`apt install enfs-dkms` once the package is published. See
[02-installation.md](02-installation.md).
