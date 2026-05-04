# Troubleshooting

This page is a symptom-driven runbook. Each entry has the same shape:

1. **Symptom** — what you observe at the shell.
2. **Signature** — what `dmesg`, the DKMS log or sysfs shows.
3. **Cause** — why it is happening.
4. **Fix** — the commands to run.
5. **Verify** — how to know the fix worked.

If your problem is not listed here, gather a `dmesg | tail -200`,
`dkms status`, `lsmod | grep -E '^(enfs|nfs|sunrpc) '`, and
`cat /proc/enfs/*/path` and open an issue.

---

## Mount succeeds but only one server gets traffic

**Symptom.** `mount -t enfs -o ...,remoteaddrs=A~B ...` returns 0,
the mount is usable, but `tcpdump` on each NFS server shows that
**only one** of the two is receiving any packets.

**Signature.**

```bash
cat /proc/enfs/0/path
# (file is missing, or shows fewer rows than addresses you specified)

ls /proc/enfs/
# directory empty
```

**Cause.** The kernel did not load **our** `nfs.ko`; it fell back to
the stock one. The stock `nfs.ko` parses but ignores `remoteaddrs=`
(it never saw the patch), so the mount silently degrades to ordinary
single-path NFS.

This usually means one of:

- The DKMS install did not complete. `dkms status -m enfs` does not
  show `installed` for the running kernel.
- `depmod` did not pick up the new modules. `modinfo -n nfs` still
  points into `kernel/`, not `updates/dkms/`.
- A consumer (most often `nfsd`, sometimes `rpcbind` in unusual
  configurations) had `sunrpc` pinned at boot, so the patched
  `sunrpc.ko` was never loaded.

**Fix.**

```bash
# Confirm which nfs.ko is being used
modinfo -n nfs

# If it points into kernel/, re-run the DKMS install:
sudo dkms install -m enfs -v 0.1.0 -k $(uname -r) --force
sudo depmod -a

# Confirm nothing has nfsd / sunrpc pinned, then unmount + remount:
lsmod | grep -E '^(nfsd|sunrpc) '
sudo umount <MOUNT_POINT>
# Make sure sunrpc unloads cleanly before remounting
sudo modprobe -r nfsv3 nfs lockd nfs_acl sunrpc
sudo mount -t enfs -o vers=3,nolock,remoteaddrs=<NFS_SERVER_1>~<NFS_SERVER_2> \
    <NFS_SERVER_1>:/<EXPORT> <MOUNT_POINT>
```

If `modprobe -r sunrpc` fails ("module is in use"), see the **`nfsd`
keeps `sunrpc` pinned** entry below — you need the blacklist plus a
reboot.

**Verify.**

```bash
modinfo -n nfs                       # should show updates/dkms/
ls /proc/enfs/                       # one directory per enfs mount
cat /proc/enfs/0/path                # one row per address you configured
```

See [04-operations.md](04-operations.md) for the full `tcpdump`
recipe for proving load actually spreads.

---

## Mount succeeds, all paths show CONNECTED, but throughput is ~1/N of expected

**Symptom.** `cat /proc/enfs/<id>/path` shows all the transports you
asked for as `Normal` + `CONNECTED|BOUND`. Round-robin across servers
looks fine. But aggregate throughput is roughly what you'd expect
from one local NIC, not N of them. tcpdump on each NIC shows that
**outbound packets all egress through one NIC** — even though their
source IP varies.

**Cause.** Your two (or more) client storage NICs are on the **same
IP subnet**. Linux's main routing table picks the first matching
route per destination, regardless of source IP. The transports enfs
created bind correctly to each source IP, but the kernel ignores
the source binding when picking a route, so all packets exit through
whichever NIC is listed first in the routing table.

This is not enfs-specific — same NIC layout breaks multi-source
behaviour for any application — but it matters here because that's
exactly the high-bandwidth multipath layout this project is built
for.

**Fix.** Source-based routing: one routing table per source IP, each
pointing at the NIC that holds that IP.

```bash
# IPv6 example — replace prefix and NIC names with yours
sudo ip -6 route add 2001:db8:2::/64 dev <NIC_A> table 101
sudo ip -6 route add 2001:db8:2::/64 dev <NIC_B> table 102
sudo ip -6 rule  add from 2001:db8:2::4:1/128 lookup 101
sudo ip -6 rule  add from 2001:db8:2::4:2/128 lookup 102

# IPv4 same idea
sudo ip -4 route add 192.0.2.0/24 dev <NIC_A> table 101
sudo ip -4 route add 192.0.2.0/24 dev <NIC_B> table 102
sudo ip -4 rule  add from 192.0.2.10/32 lookup 101
sudo ip -4 rule  add from 192.0.2.11/32 lookup 102

sudo umount /mnt && sudo mount -t enfs ... /mnt    # re-establish sockets
```

**Verify.**

```bash
sudo tcpdump -i <NIC_A> -c 100 'ip6 and dst port 2049' \
    | awk '{print $3}' | sed 's/\.[0-9]*$//' | sort -u
# Should print just the NIC's own source IP — never the other NIC's.
```

**Persist** the rules across reboots via netplan / systemd-networkd /
NetworkManager / `iptables-restore`-style helper of your choice;
exact mechanism depends on the distro and how the host is configured.

Lab measurement: with this fix in place, a 16-stream parallel read
went from ~40 MB/s (one NIC bottleneck) to **636 MB/s** (15.5×
scaling on 16 transports). See issue #23.

---

## "Stale file handle" right after mount

**Symptom.** Mount succeeds, but the very first `ls` or `cat` against
the mount point — sometimes only the second or third one — fails with
`Stale file handle`.

**Signature.**

```text
dmesg:
NFS: server <NFS_SERVER_2> error: fileid changed
NFS: server <NFS_SERVER_2> not responding, still trying
```

**Cause.** The `remoteaddrs=` set points at servers that **do not
back the same export**. The dispatcher hands a file handle issued by
server A to server B; B has no idea what it is. The most common
sub-cause is that two servers export the same path but with different
`fsid=` values in `/etc/exports`.

**Fix.** On every server in the `remoteaddrs=` set, ensure the export
shares the same `fsid=`:

```text
# /etc/exports on every server backing this multipath mount
/<EXPORT>  *(rw,fsid=<SHARED_FSID>,no_subtree_check,...)
```

Restart the NFS server (`sudo exportfs -ra`), unmount on the client,
remount.

For mirrored-but-independent storage (e.g. two servers each holding a
copy of the same data via rsync), enfs is **not** the right tool —
see [01-overview.md](01-overview.md) under "When NOT to use enfs".

**Verify.** No more `Stale file handle` after a fresh mount; no
`fileid changed` lines in `dmesg`. A directory listing returns
identical inodes regardless of which transport serves the call.

---

## `modprobe enfs` returns "Invalid argument"

**Symptom.**

```bash
sudo modprobe enfs
# modprobe: ERROR: could not insert 'enfs': Invalid argument
```

**Signature.**

```text
dmesg:
sunrpc: version magic '...' should be '...'
enfs: disagrees about version of symbol ...
```

Or:

```bash
lsmod | grep nfsd
# nfsd  ...
```

**Cause.** The stock `nfsd` module is loaded; it pulled in the stock
`sunrpc.ko` at boot before our patched one had a chance to load.
The kernel will not let two `sunrpc` builds coexist, so `enfs.ko`
(which was built against our patched `sunrpc`) is rejected with
`EINVAL`.

**Fix.** Blacklist `nfsd` and reboot once:

```bash
sudo tee /etc/modprobe.d/zz-no-nfsd.conf > /dev/null <<'EOF'
blacklist nfsd
install nfsd /bin/true
EOF

sudo update-initramfs -u
sudo reboot
```

Full procedure is in [02-installation.md](02-installation.md) under
"Loading: blacklist `nfsd`".

**Verify.**

```bash
lsmod | grep nfsd     # empty
lsmod | grep sunrpc   # empty until you mount
sudo mount -t enfs -o vers=3,nolock,remoteaddrs=<NFS_SERVER_1>~<NFS_SERVER_2> \
    <NFS_SERVER_1>:/<EXPORT> <MOUNT_POINT>
lsmod | grep -E '^(enfs|nfs|sunrpc) '   # all loaded from updates/dkms/
```

---

## DKMS build fails after a kernel update

**Symptom.** `apt upgrade` brought in a new kernel; on next boot,
`modinfo -n nfs` still points into `kernel/`, not `updates/dkms/`.
Or, during the upgrade, `dkms install` printed a build failure.

**Signature.**

```bash
dkms status -m enfs
# enfs/0.1.0, 7.0.0-15-generic, x86_64: failed
```

The full build log lives at:

```text
/var/lib/dkms/enfs/0.1.0/build/make.log
```

Common causes the log will show:

- A new symbol the patched files haven't been adapted for.
- `linux-headers-$(uname -r)` not installed — `apt-get install
  linux-headers-$(uname -r)` and re-run.
- A kernel ABI bump that touched a struct enfs hooks into.

**Fix.**

```bash
# First, make sure headers are present:
sudo apt install linux-headers-$(uname -r)

# Then re-attempt the build:
sudo dkms install -m enfs -v 0.1.0 -k $(uname -r) --force
```

If the build log shows a real source-level incompatibility (not a
missing-headers issue), capture the relevant section and open an
issue. Pin the previous kernel via `grub` while you wait — the older
kernel still has working modules under its own `updates/dkms/`.

**Verify.**

```bash
dkms status -m enfs         # all installed kernels show "installed"
modinfo -n nfs              # updates/dkms/ for the current kernel
```

---

## "disagrees about version of symbol" from one specific module

**Symptom.** Most enfs modules load fine, but one (typically
`nfs_acl` or `lockd`) fails with a symbol-version mismatch.

**Signature.**

```text
dmesg:
nfs_acl: disagrees about version of symbol <some_symbol>
nfs: Unknown symbol <some_symbol> (err -22)
```

**Cause.** A previous troubleshooting attempt blacklisted one of the
six modules enfs ships, or removed a single `.ko` from
`updates/dkms/` by hand. The remaining DKMS modules now reference a
symbol version (`__versions` CRC) that the surviving stock module
doesn't export the same way.

**Fix.** Force a full re-install so all six modules go back in
together:

```bash
sudo dkms install -m enfs -v 0.1.0 -k $(uname -r) --force
sudo depmod -a

# Make sure no enfs module is blacklisted by accident:
grep -r 'blacklist.*\(sunrpc\|nfs\|nfs_acl\|lockd\|nfsv3\|enfs\)' \
    /etc/modprobe.d/

# Drop everything and remount:
sudo umount <MOUNT_POINT>
sudo modprobe -r nfsv3 nfs lockd nfs_acl sunrpc enfs 2>/dev/null
sudo mount -t enfs -o vers=3,nolock,remoteaddrs=<NFS_SERVER_1>~<NFS_SERVER_2> \
    <NFS_SERVER_1>:/<EXPORT> <MOUNT_POINT>
```

**Verify.**

```bash
lsmod | grep -E '^(enfs|nfs|nfs_acl|nfsv3|lockd|sunrpc) '
# All six lines present
modinfo $(modinfo -n nfs_acl) | grep filename
# Should be under updates/dkms/
```

---

## Cross-references

- For day-2 inspection commands referenced above, see
  [04-operations.md](04-operations.md).
- To roll the whole package back to stock, see
  [06-uninstall.md](06-uninstall.md).
