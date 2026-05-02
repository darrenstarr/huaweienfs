# Installation

This page walks you through getting the enfs modules onto an Ubuntu
26.04 LTS host, verifying that they are the ones the kernel will load,
and arranging for them to come up cleanly after a reboot.

If you have not yet read [01-overview.md](01-overview.md), do that
first — in particular the **"When NOT to use enfs"** section, which
calls out that this package is **not safe on root-on-NFS systems**.

## Prerequisites

You need the running kernel's headers and the DKMS toolchain.

```bash
sudo apt update
sudo apt install dkms build-essential linux-headers-generic
```

`linux-headers-generic` keeps the headers package tracking your
running kernel through future `apt upgrade`s, which is what lets DKMS
auto-rebuild on every kernel ABI bump.

You should also have a working stock NFSv3 mount before you begin.
If `mount -t nfs -o vers=3 <NFS_SERVER>:/<EXPORT> /mnt` does not work
against your storage today, fix that first — enfs only adds multipath
on top of a working NFS client, it does not replace any plumbing
underneath it.

## Path A — install from a `.deb` (recommended once published)

> Status: **planned**. The DKMS source package builds today; published
> apt repository hosting is on the v0.1 roadmap. Use Path B until this
> is live.

Once a release archive is available you will be able to:

```bash
# Add the apt source (URL TBD with first published release)
curl -fsSL https://example.invalid/enfs/keyring.gpg | \
    sudo tee /etc/apt/keyrings/enfs.gpg > /dev/null
echo "deb [signed-by=/etc/apt/keyrings/enfs.gpg] \
    https://example.invalid/enfs/apt resolute main" | \
    sudo tee /etc/apt/sources.list.d/enfs.list

sudo apt update
sudo apt install enfs-dkms
```

`apt install` invokes the DKMS hooks that build the three modules
against your installed `linux-headers-$(uname -r)`. Skip ahead to
**Verifying the install**.

## Path B — install from source (current)

Clone the repository on the target host:

```bash
git clone https://github.com/<ORG>/enfs-dkms.git
cd enfs-dkms
```

Build and install via DKMS in one shot:

```bash
sudo make dkms-install
```

What this does, in order:

1. Materialises `src/` from `vendor/` + `patches/` (`make port`).
2. Copies `src/` to `/usr/src/enfs-<VERSION>/`.
3. Runs `dkms add -m enfs -v <VERSION>`.
4. Runs `dkms build -m enfs -v <VERSION> -k $(uname -r)`.
5. Runs `dkms install -m enfs -v <VERSION> -k $(uname -r)`.
6. Refreshes the module index with `depmod -a`.

The build produces six kernel modules (`sunrpc.ko`, `nfs_acl.ko`,
`lockd.ko`, `nfs.ko`, `nfsv3.ko`, `enfs.ko`) and DKMS installs them
under:

```text
/lib/modules/$(uname -r)/updates/dkms/
```

`updates/` outranks `kernel/` in `depmod` order, so subsequent
`modprobe nfs` / `modprobe sunrpc` will pick up our patched copies in
preference to the stock ones.

## Verifying the install

Check that the running kernel's view of `nfs.ko` now points at our
DKMS build, not the stock kernel package:

```bash
modinfo -n nfs
```

Expected — note `updates/dkms/`, **not** `kernel/`:

```text
/lib/modules/7.0.0-15-generic/updates/dkms/nfs.ko
```

Look at the module metadata. The `vermagic` should match your running
kernel exactly, and `enfs.ko` should be authored by Huawei:

```bash
modinfo $(modinfo -n nfs)        # vermagic, srcversion, depends
modinfo $(modinfo -n enfs)       # author: Huawei Technologies Co., Ltd
```

Ask DKMS what it has registered:

```bash
dkms status -m enfs
```

Expected — one row per installed kernel:

```text
enfs/0.1.0, 7.0.0-15-generic, x86_64: installed
```

Once you load the modules (next section), confirm they are live:

```bash
lsmod | grep -E '^(enfs|nfs|sunrpc) '
```

You should see `enfs` listed alongside `nfs`, `nfsv3` and `sunrpc`.

## Loading: blacklist `nfsd` so `sunrpc` is not pinned at boot

Stock Ubuntu auto-loads the `nfsd` kernel server module from the
`nfs-kernel-server` package's systemd unit. `nfsd` depends on
`sunrpc`, which means `sunrpc` becomes pinned in memory before any
multipath consumer ever asks for it — and once a module is in use, the
kernel will not let you swap it out for our patched copy. The symptom
is `modprobe enfs` returning `Invalid argument`, even though the file
is present and `modinfo` finds it.

The fix is to keep `nfsd` from auto-loading on this host. (If you
actually need to **serve** NFS from this same host, this DKMS package
is not the right tool — see [01-overview.md](01-overview.md).)

Create a blacklist drop-in:

```bash
sudo tee /etc/modprobe.d/zz-no-nfsd.conf > /dev/null <<'EOF'
# enfs-dkms: prevent stock nfsd from pinning sunrpc at boot,
# which would block modprobe of our patched sunrpc.ko.
blacklist nfsd
install nfsd /bin/true
EOF
```

Update the initramfs so the blacklist applies during early boot:

```bash
sudo update-initramfs -u
```

**Reboot once.** This is the only reboot the install requires, and
it is needed because the running `sunrpc` was loaded by the stock
modules at last boot and cannot be swapped under a live system.

```bash
sudo reboot
```

After the reboot, verify that `nfsd` did **not** load and that nothing
else has pinned `sunrpc`:

```bash
lsmod | grep nfsd      # should produce no output
lsmod | grep sunrpc    # should be empty until you mount something
```

Now do a multipath mount. The kernel will load `sunrpc`, `nfs_acl`,
`lockd`, `nfs`, `nfsv3` and `enfs` on demand from `updates/dkms/`:

```bash
sudo mkdir -p /mnt/enfs
sudo mount -t nfs \
    -o vers=3,nolock,remoteaddrs=<NFS_SERVER_1>~<NFS_SERVER_2> \
    <NFS_SERVER_1>:/<EXPORT> /mnt/enfs

lsmod | grep -E '^(enfs|nfs|sunrpc) '
ls /proc/enfs/                # one directory per enfs-managed mount
```

If `/proc/enfs/` is empty after a multipath mount, jump to
[05-troubleshooting.md](05-troubleshooting.md) — the most common cause
is that the kernel fell back to the stock `nfs.ko`, which silently
ignores `remoteaddrs=`.

## Surviving kernel upgrades

DKMS is registered as an `apt` trigger. When `apt upgrade` brings in
a new kernel ABI (e.g. 7.0.0-14 → 7.0.0-15), DKMS rebuilds enfs
against the new headers as part of the upgrade transaction. After
reboot, `modinfo -n nfs` should still show `updates/dkms/` for the new
kernel.

This has been verified in practice across `7.0.0-14` → `7.0.0-15`.
If it ever fails, see [05-troubleshooting.md](05-troubleshooting.md)
under "DKMS build fails after kernel update".
