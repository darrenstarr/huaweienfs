% ENFS(7) | enfs-dkms manual
% enfs-dkms maintainers
% May 2026

# NAME

enfs - Enhanced NFS multipath client for Linux

# SYNOPSIS

**mount -t enfs -o** *opts*[**,remoteaddrs=***addr1*~*addr2*~...][**,localaddrs=***addr*~...] *server*:*/export* *mountpoint*

# DESCRIPTION

**enfs** is the Enhanced NFS client originally developed by Huawei
for the OpenEuler Linux distribution and ported here as a DKMS
package for Ubuntu LTS kernels. It augments the in-tree Linux NFS
client with:

* **multipath mounts** — a single NFS mount uses several server
  addresses concurrently; each NFS RPC is dispatched round-robin
  across the live transports.
* **transport failover** — when a server address stops responding to
  liveness probes, traffic moves to the surviving paths without
  hanging the mount or returning errors to user-space.
* **runtime path management** — the per-mount path list and per-path
  state are visible and (in some cases) controllable through
  **/proc/enfs/**.

The package consists of six kernel modules that together replace the
host's in-tree NFS client stack:

* **sunrpc.ko** — stock SunRPC plus the *sunrpc_enfs_adapter*
  registration glue.
* **nfs.ko** — stock NFS client plus the *enfs_adapter* glue.
* **nfsv3.ko** — NFSv3 client procedures plus the enfs *EXTEND* op.
* **lockd.ko** — NLM lock manager, rebuilt against the patched sunrpc
  CRCs so its symbol versions resolve cleanly.
* **nfs_acl.ko** — NFSv3 ACL XDR, rebuilt for the same reason.
* **enfs.ko** — the new multipath, failover and round-robin module.

DKMS installs all six into _/lib/modules/$(uname -r)/updates/dkms/_,
which **depmod** ranks above _kernel/_, so subsequent **modprobe nfs**
or **modprobe sunrpc** picks up the patched copies. **dkms remove**
restores the stock modules.

# MOUNT OPTIONS

These options are recognised by the patched **fs/nfs/fs_context.c** in
addition to the standard NFS mount options documented in **nfs(5)**.

**remoteaddrs=**_addr1_[~_addr2_~...]
:   List of remote (server) addresses to spread RPCs across, separated
    by tilde (**~**). Each address may be IPv4 or IPv6 (square-bracket
    form for IPv6). At least one of these must be reachable when the
    mount call is made; the rest are probed asynchronously and added
    to the round-robin set as they come up.

**localaddrs=**_addr1_[~_addr2_~...]
:   List of local (client-side) source addresses to bind outgoing
    RPCs to. Useful when the client has multiple NICs in different
    VLANs and you want enfs to spread outbound traffic across them in
    addition to the server side. If omitted, enfs lets the kernel
    pick the source address per route.

**enfs_info=**_string_
:   Legacy compatibility alias for prior OpenEuler mount syntax. The
    parser accepts and ignores it; put your server list in
    **remoteaddrs=** and your client list in **localaddrs=**.

**slookupcache=**_seconds_, **alookupcache=**_seconds_
:   Server-side and arm-side lookup-cache timeouts for the enfs DNS
    rebind helper. Defaults are conservative; raise only if you
    re-resolve hostnames frequently.

All standard **nfs(5)** options (**vers=**, **proto=**, **rsize=**,
**wsize=**, **soft**, **hard**, **timeo=**, **retrans=**, **nolock**,
etc.) work as usual on top of these.

# RUNTIME INTERFACES

## /sys/kernel/sunrpc/xprt-switches/

The standard SunRPC sysfs view of transport switches. Each NFS mount
backed by enfs has an entry; per-switch:

**xprt_switch_info**
:   Counts: **num_xprts**, **num_active**, **num_unique_destaddr**,
    **queue_len**. After a multipath mount with N
    **remoteaddrs**, **num_xprts** and **num_active** should both
    equal N. If they don't, traffic is going through fewer transports
    than you asked for.

**switch-N/xprt-M-tcp/xprt_info**
**switch-N/xprt-M-tcp/xprt_state**
**switch-N/xprt-M-tcp/srcaddr**
**switch-N/xprt-M-tcp/dstaddr**
:   Per-transport status. **xprt_state** flips through CONNECTED,
    BOUND, etc.; values are documented in the kernel's
    **net/sunrpc/xprtsock.c**.

## /proc/enfs/

Created by the **enfs_proc** subsystem (see **enfs_proc.c**) when
**enfs.ko** loads. The directory exists once the module is loaded
even if no enfs-managed mounts are active. Per-mount entries appear
when **cl_enfs == 1** clients are created.

## /proc/sys/sunrpc/nfs_debug

Bitmask for NFS debug output. enfs adds a new bit:
`NFSDBG_ENFS = 0x10000`.

```sh
echo 0x10000 | sudo tee /proc/sys/sunrpc/nfs_debug
sudo dmesg -w
```

emits enfs's per-RPC and per-path-state-change traces.

# EXAMPLES

## Multipath mount across three server VIPs

```sh
sudo mount -t enfs -o nolock,vers=3,proto=tcp,\
  remoteaddrs=10.0.0.10~10.0.0.11~10.0.0.12 \
  10.0.0.10:/export /mnt/multi
```

After the mount returns, verify:

```sh
sudo cat /sys/kernel/sunrpc/xprt-switches/switch-*/xprt_switch_info
```

The output should show **num_xprts=3** and **num_active=3**.

## Multipath mount with both client- and server-side fan-out

```sh
sudo mount -t enfs -o nolock,vers=3,proto=tcp,\
  localaddrs=192.168.10.5~192.168.20.5,\
  remoteaddrs=10.0.0.10~10.0.0.11~10.0.0.12~10.0.0.13 \
  10.0.0.10:/export /mnt/multi
```

This uses 2 client NICs to talk to 4 server VIPs, so
**num_xprts=8** (the cross product) once all paths are up.

## Watching a controller failure

In one terminal, generate read load:

```sh
dd if=/mnt/multi/largefile of=/dev/null bs=1M count=1024 status=progress
```

In another, drop one server NIC (e.g., shut down its switchport).
After the **pm_ping** interval (5s default), **dmesg** should show:

```text
enfs:[pm_set_path_state] The xprt localip{*} remoteip{10.0.0.11} \
  path state change from {1} to {0}.
```

The **dd** in the first terminal does not pause or fail. **xprt_switch_info**
now shows **num_active=N-1**.

## Loading the EXTEND debug bit

```sh
echo 0x10000 | sudo tee /proc/sys/sunrpc/nfs_debug
sudo dmesg -c >/dev/null
sudo mount -t enfs ... # as above
sudo dmesg | tail -20
```

Look for **enfs_parse_mount_options**, **enfs_create_multi_xprt** and
**pm_set_path_state** traces. If none appear, the mount didn't go
through the enfs path — re-check **remoteaddrs=** spelling and that
**enfs.ko** is loaded (**lsmod | grep enfs**).

# FILES

_/lib/modules/$(uname -r)/updates/dkms/sunrpc.ko_
_/lib/modules/$(uname -r)/updates/dkms/nfs.ko_
_/lib/modules/$(uname -r)/updates/dkms/nfsv3.ko_
_/lib/modules/$(uname -r)/updates/dkms/lockd.ko_
_/lib/modules/$(uname -r)/updates/dkms/nfs_acl.ko_
_/lib/modules/$(uname -r)/updates/dkms/enfs.ko_

_/usr/src/enfs-_*VERSION*_/_
:   DKMS source tree shipped by the **enfs-dkms** Debian package.

_/etc/enfs/config.ini_
:   Optional: legacy OpenEuler-style config file. enfs probes for it
    at module load and logs a *Failed to open file* message to
    **dmesg** if absent — that is harmless.

# CAVEATS

* **First boot after install** — DKMS installs the modules into
  _/lib/modules/.../updates/dkms/_, but on Ubuntu 24.04 the patched
  modules do not load until the initramfs is rebuilt to include
  them. The package's postinst rebuilds the initramfs automatically;
  a reboot is still required for the new initramfs to take effect.
* **Server compatibility** — the **EXTEND** NFSv3 op
  (**NFS3PROC_EXTEND = 22**) is a Huawei extension. Generic Linux
  **nfsd** does not implement it; sending an EXTEND RPC to such a
  server returns **NFS3ERR_NOTSUPP**. The mount itself works against
  any NFS server; only enfs's extension queries are server-specific.
* **NFS-over-RDMA**, **pNFS** and **NFSv4 referrals** are not in
  scope of this port.
* Modifications to **sunrpc.ko** are gated by the ****GENKSYMS****
  trick so that stock **lockd** / **nfs_acl** / **nfsd** continue to
  load against our patched **sunrpc.ko** without symbol-version
  errors. If you replace **enfs-dkms** with a custom build that
  doesn't preserve this gate, expect *disagrees about version of
  symbol* errors at module load.

# SEE ALSO

**nfs(5)**, **mount.nfs(8)**, **rpcinfo(8)**, **showmount(8)**,
**dkms(8)**, **modprobe(8)**, **depmod(8)**.

The package's user-facing documentation lives under
_/usr/share/doc/enfs-dkms/_ and online at
<https://github.com/darrenstarr/huaweienfs/tree/main/docs/user>.

The OpenEuler upstream of the **enfs** kernel code is at
<https://gitee.com/openeuler/kernel/tree/OLK-6.6/fs/nfs/enfs>.

# AUTHORS

Original **enfs** kernel code by Huawei Technologies, integrated into
the OpenEuler kernel branch **OLK-6.6**. Ubuntu DKMS port
maintained by the project owner with substantial assistance from
Anthropic Claude Opus 4.7.

# COPYRIGHT

GPL-2.0-only. The vendored Linux kernel sources retain their
original SPDX/copyright headers verbatim. See
_/usr/share/doc/enfs-dkms/copyright_ for full attribution.
