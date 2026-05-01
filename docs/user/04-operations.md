# Operating an enfs Mount

Once a multipath mount is up (see
[03-mount-syntax.md](03-mount-syntax.md)), enfs exposes its live state
under `/proc/enfs/` and `/sys/kernel/sunrpc/`. This page covers the
day-2 control surface: inspecting the path set, editing it at runtime,
forcing a DNS rebind, and proving with `tcpdump` that load really is
spreading the way you think it is.

## The `/proc/enfs/` tree

Every enfs-managed mount creates a directory under `/proc/enfs/`,
keyed by an internal client ID:

```bash
ls /proc/enfs/
# 0  1  2 ...
```

Each per-mount directory contains, at minimum, two files:

| File | Purpose |
|------|---------|
| `path` | Live path set: one row per `(local, remote)` transport |
| `stat` | Per-path counters (RPCs sent, completed, failed, retries) |

### `/proc/enfs/<id>/path`

Shows every transport in the mount's switch and its current state.

```bash
cat /proc/enfs/0/path
```

Sample output:

```
id  local_addr        remote_addr       path_state  xprt_state
0   <LOCAL_NIC_1>     <NFS_SERVER_1>    active      connected
1   <LOCAL_NIC_1>     <NFS_SERVER_2>    active      connected
```

Column meanings:

- **`id`** — switch-local index. Stable for the life of the mount.
- **`local_addr`** — source IP this transport is bound to.
- **`remote_addr`** — destination NFS server IP.
- **`path_state`** — enfs's view: `active`, `failed`, `draining`,
  or `init`. Round-robin only dispatches to `active`.
- **`xprt_state`** — sunrpc's view of the underlying TCP transport:
  `connected`, `connecting`, `closed`, or `bound`.

If `path_state=active` but `xprt_state=closed` for more than a few
seconds, the failover state machine has not yet noticed; it usually
catches up on the next RPC.

### `/proc/enfs/<id>/stat`

Per-path RPC counters, useful for verifying load is actually spreading
and for spotting a slow-but-not-dead path.

```bash
cat /proc/enfs/0/stat
```

The columns vary by build but always include sent/completed/failed
RPC counts per transport id. To watch live:

```bash
watch -n 1 'cat /proc/enfs/0/stat'
```

If one path is taking 10x as many RPCs as the others, the
round-robin counter has lost its place — usually because the others
went `failed` for a moment. Cross-check with `path_state` from
`/proc/enfs/0/path`.

## Adding and removing paths at runtime

The `/proc/enfs/<id>/path` file is **writable**. You can add a server
IP, remove one, or replace the entire set without unmounting the
share. Open file descriptors keep working — RPCs that were in flight
on a removed transport are re-dispatched on a surviving one.

The currently supported write commands:

```bash
# Add a remote address
echo 'add_remote <NFS_SERVER_3>' | sudo tee /proc/enfs/0/path

# Remove a remote address
echo 'remove_remote <NFS_SERVER_2>' | sudo tee /proc/enfs/0/path

# Add a local source address (a new client NIC came online)
echo 'add_local <LOCAL_NIC_2>' | sudo tee /proc/enfs/0/path
```

After each write, re-read `path` to confirm the new switch state:

```bash
cat /proc/enfs/0/path
```

Any address you remove with `remove_remote` is dropped from the
rotation immediately and its TCP transport is torn down once
in-flight RPCs drain.

> The exact verb set is intentionally small in v0. If you need a
> richer control surface (priorities, weights, drain timeouts), open
> an issue describing the operational scenario.

## Live remount

The mount-time options can be edited as a whole using the standard
`mount -o remount` mechanism:

```bash
sudo mount -o remount,remoteaddrs=<NFS_SERVER_1>~<NFS_SERVER_3> \
    <MOUNT_POINT>
```

A live remount goes through the same parser as the initial mount and
ends up calling the same path-set update code as the `/proc` writes
above. Use it when:

- You want the change captured in `/proc/mounts` so it shows up in
  config-management drift detection.
- You are scripting against `mount` and don't want a second tool.

A live remount **cannot** change `vers=`, `proto=` or other
kernel-NFS-client fundamentals — only the enfs-specific options.

## DNS rebind

If you mounted using a hostname that backs a multi-A-record DNS entry
(or that has since had records added/removed), you can ask enfs to
re-resolve and merge:

```bash
echo 'dns_rebind' | sudo tee /proc/enfs/0/path
```

What to expect:

1. The hostname recorded at mount time is re-resolved.
2. Any new addresses are added as fresh transports.
3. Addresses that disappeared from DNS are **not** automatically
   removed (a DNS blip would otherwise drop your paths). Remove them
   explicitly with `remove_remote` if you want.

`dmesg` will log the resolved address set so you can correlate with
your DNS change.

## sysfs view: the underlying transport switch

enfs sits on top of sunrpc's transport-switch primitive. sunrpc
exposes one switch per active multipath mount:

```bash
ls /sys/kernel/sunrpc/xprt-switches/
# switch-0  switch-1 ...

cat /sys/kernel/sunrpc/xprt-switches/switch-0/xprt_switch_info
```

Sample:

```
num_xprts=2 num_active=2 queue_len=0
```

Column meanings:

- **`num_xprts`** — total transports in this switch (i.e. paths
  in this mount's set, including failed ones).
- **`num_active`** — how many of those transports are currently
  eligible for dispatch. If `num_active < num_xprts`, at least one
  path is in failure recovery.
- **`queue_len`** — RPCs currently queued waiting for an active
  transport. Should hover near zero on a healthy mount.

The sysfs view is a useful sanity check when `/proc/enfs/<id>/path`
disagrees with what you think the mount should look like — sysfs is
the ground truth from the RPC layer, `/proc/enfs/` is enfs's
projection of that.

## Watching multipath in action with `tcpdump`

The most direct way to prove that load really is spreading is to
sniff the wire on each server interface and count packets.

On the client, kick off a sustained read:

```bash
dd if=<MOUNT_POINT>/<LARGE_FILE> of=/dev/null bs=1M count=4096
```

On each NFS server (or on the client, filtering by destination IP),
in parallel:

```bash
sudo tcpdump -i any -nn -c 1000 \
    'tcp and port 2049 and host <CLIENT_IP>' \
    > /tmp/server1.tcpdump

sudo tcpdump -i any -nn -c 1000 \
    'tcp and port 2049 and host <CLIENT_IP>' \
    > /tmp/server2.tcpdump
```

Then compare packet counts. With a two-server multipath mount and
a streaming read workload, you should see a roughly 50/50 split.
Anything more skewed than 60/40 over a 10-second window is worth
investigating — usually the cause is one transport in `connecting`
or `failed` state for part of the run; cross-check with
`/proc/enfs/<id>/path`.

This is the same observation that was used to verify the v0 release;
it is the canonical "is it really working?" test.

## Cross-references

- Symptoms and fixes: [05-troubleshooting.md](05-troubleshooting.md)
- Removing the package cleanly: [06-uninstall.md](06-uninstall.md)
