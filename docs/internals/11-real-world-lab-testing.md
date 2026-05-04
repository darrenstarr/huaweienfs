# 11. Real-world lab testing procedure

> Prerequisites: [chapter 1](./01-architecture.md) (the build pipeline)
> and [chapter 10](./10-testing-and-ci.md) (CI surface and test
> framework). This chapter covers the **on-iron** load-sharing
> validation against a high-bandwidth NFS storage cluster, separate
> from CI's compile/install checks.

CI proves the modules build and load. The userspace test framework
([§10.11](./10-testing-and-ci.md#1011-userspace-unit-tests-tests-make-test))
proves the algorithms behave. Neither exercises the failure modes
that only show up against real storage at line rate — server-side
queuing, multi-NIC routing, jumbo-frame MTU mismatches, OceanStor
NFSv3 EXTEND-op quirks. This chapter is the procedure for that
validation.

The reference setup ("the lab", anonymised below) is a single
high-end server fronting a Huawei OceanStor Pacific cluster across
8 server-side NIC IPs over an isolated 9000+ MTU IPv6 storage
fabric, with the client carrying two storage NICs into the same
VLAN. Real values live in `secrets/<lab-host>.md` (gitignored);
public docs use placeholders.

## 11.1 Topology

```text
                          ┌──────────────────────┐
                          │ NFS storage cluster  │
                          │ (8 controller NICs)  │
                          │ <SRV1>..<SRV8>       │
                          │ all bound to /export │
                          └─┬──┬──┬──┬──┬──┬──┬──┘
                            │  │  │  │  │  │  │
              storage VLAN, 9134-MTU IPv6 fabric
                            │  │  │  │  │  │  │
                          ┌─┴──┴──┴──┴──┴──┴──┴──┐
                          │ <LAB_HOST> client    │
                          │ <NIC_A>=<LOCAL_A>    │
                          │ <NIC_B>=<LOCAL_B>    │
                          │ Ubuntu 26.04 / 7.0   │
                          │ enfs-dkms <ver>      │
                          └──────────────────────┘
```

| Placeholder | Means |
|---|---|
| `<LAB_HOST>` | The lab client server (single Ubuntu host) |
| `<NIC_A>` / `<NIC_B>` | The two storage NICs (typically VLAN sub-interfaces in this setup) |
| `<LOCAL_A>` / `<LOCAL_B>` | The IPv6 addresses bound to those NICs |
| `<SRV1>`..`<SRV8>` | The 8 storage controller NIC IPs |
| `<PREFIX>::/64` | The storage VLAN's IPv6 prefix |
| `<EXPORT>` | The shared NFS export path on the storage |

## 11.2 First-time setup checklist

Validate before the first multipath mount:

```bash
# 1. Modules loaded
lsmod | grep -E '^(enfs|nfs|nfsv3|sunrpc|lockd)\b'
# Expect: all 5 + enfs

# 2. Both storage NICs up, in the same /64
ip -6 addr show dev <NIC_A> | grep inet6
ip -6 addr show dev <NIC_B> | grep inet6

# 3. Each storage server reachable from each NIC
for SRV in <SRV1> ... <SRV8>; do
    ping6 -I <NIC_A> -c1 $SRV >/dev/null && echo OK A→$SRV || echo FAIL
    ping6 -I <NIC_B> -c1 $SRV >/dev/null && echo OK B→$SRV || echo FAIL
done

# 4. MTU matches across the fabric (jumbo frames need everyone to agree)
ip -d link show <NIC_A> | grep -i mtu
# Expect the configured MTU (e.g. 9134); a mismatch causes silent
# fragmentation and ~10× perf hit
```

## 11.3 Source-based routing (required when NICs share a /64)

The high-bandwidth dual-NIC layout typically has both NICs in the
same `<PREFIX>::/64`. Linux's main routing table picks one NIC per
destination, so all outbound RPCs egress through one NIC even with
multipath transports correctly bound to two source IPs. See
[user/03-mount-syntax.md](../user/03-mount-syntax.md#ipv6-multipath-8-server--2-nic-layout-validated)
and issue #23.

```bash
sudo ip -6 route add <PREFIX>::/64 dev <NIC_A> table 101
sudo ip -6 route add <PREFIX>::/64 dev <NIC_B> table 102
sudo ip -6 rule  add from <LOCAL_A>/128 lookup 101
sudo ip -6 rule  add from <LOCAL_B>/128 lookup 102

# Confirm
ip -6 route get <SRV1> from <LOCAL_A>   # should select <NIC_A>
ip -6 route get <SRV1> from <LOCAL_B>   # should select <NIC_B>
```

These rules don't survive reboot by default. Persist via netplan /
systemd-networkd / `/etc/network/interfaces.d/` per local convention.

## 11.4 Mount

```bash
sudo mount -t enfs \
    -o vers=3,nolock,proto=tcp6,\
remoteaddrs=<SRV1>~<SRV2>~<SRV3>~<SRV4>~<SRV5>~<SRV6>~<SRV7>~<SRV8>,\
localaddrs=<LOCAL_A>~<LOCAL_B> \
    [<SRV1>]:/<EXPORT> /mnt
```

Within ~1 s, `dmesg | tail -30` should show 16 transports moving
from path-state 0 to 1 (PM_STATE_INIT → PM_STATE_NORMAL) — one log
line per `(local, remote)` pair.

## 11.5 Path-table verification

```bash
ls /proc/enfs/                             # one dir per enfs mount
cat /proc/enfs/<id>/path                   # 16 rows expected
```

Expected output (anonymised):

```text
id  local_addr        remote_addr       path_state  xprt_state
0   <LOCAL_A>         <SRV1>            Normal      CONNECTED|BOUND
1   <LOCAL_B>         <SRV2>            Normal      CONNECTED|BOUND
...
15  <LOCAL_B>         <SRV1>            Normal      CONNECTED|BOUND
```

All 16 rows must show `Normal` + `CONNECTED|BOUND`. Any `Fault` or
`Init` row means that path's TCP handshake failed — usually MTU
mismatch, ACL on the storage, or the routing rules above being
absent.

## 11.6 Single-stream baseline

```bash
sudo bash -c "echo 3 > /proc/sys/vm/drop_caches"
time dd if=/mnt/<largefile> of=/dev/null bs=1M count=4096 \
    skip=$RANDOM_OFFSET iflag=direct
```

Use a `skip=` value that's not in the storage's read-ahead cache
(several GiB beyond anything you've recently touched) — server-side
caching can skew results by 5×–10×.

Single-stream `iflag=direct` only ever has **one RPC in flight at a
time** because direct I/O is synchronous. Expect throughput on the
order of `RPC payload (1 MiB) / round-trip latency`, not anything
near the link rate. Lab number for reference: ~40 MB/s for a single
stream over an 8-server, 2-NIC IPv6 multipath.

## 11.7 Parallel-stream scaling test (the real thing)

This is what proves multipath actually distributes load.

```bash
sudo bash -c "echo 3 > /proc/sys/vm/drop_caches"
START=$(date +%s.%N)
for i in $(seq 0 15); do                   # 16 parallel readers
    skip=$((150000 + i * 1024))            # each at a different offset
    dd if=/mnt/<largefile> of=/dev/null bs=1M count=1024 \
        skip=$skip iflag=direct &
done
wait
END=$(date +%s.%N)
ELAPSED=$(echo "$END - $START" | bc)
echo "16 streams × 1 GiB = 16384 MiB in ${ELAPSED}s"
```

Reference numbers from the lab on `<LAB_HOST>` (8 OceanStor servers,
2 client NICs, source routing in place):

| Streams | Throughput | Scaling vs 1-stream |
|---|---|---|
| 1 | 41 MB/s | 1.0× (baseline) |
| 4 | 170 MB/s | 4.1× |
| 8 | 326 MB/s | 7.9× |
| 16 | 636 MB/s | 15.5× |

Near-perfect scaling all the way to 16 transports: that's the
primary success signal. Anything significantly sub-linear (e.g. 8
streams getting only 3× the 1-stream throughput) means traffic
isn't actually distributed and there's a host config problem to
chase down (most likely: missing source routing, or one of the
storage controllers is degraded).

## 11.8 Per-NIC distribution proof (tcpdump)

To confirm both NICs are actively carrying outbound traffic:

```bash
# Pre-flight: clean any leftover dumps
sudo pkill -9 tcpdump 2>/dev/null
sudo rm -f /tmp/da.pcap /tmp/db.pcap

# Capture both NICs while a 4-GiB read runs
sudo tcpdump -i <NIC_A> -nn ip6 and port 2049 -w /tmp/da.pcap &
sudo tcpdump -i <NIC_B> -nn ip6 and port 2049 -w /tmp/db.pcap &
sleep 2
sudo dd if=/mnt/<largefile> of=/dev/null bs=1M count=4096 \
    skip=$BIG_UNCACHED_OFFSET iflag=direct
sleep 2
sudo pkill -INT tcpdump
wait

# Outbound src-IP distribution per NIC
for nic_pcap in da.pcap db.pcap; do
    echo "=== /tmp/$nic_pcap OUTBOUND ==="
    sudo tcpdump -nn -r /tmp/$nic_pcap 'ip6 and dst port 2049' 2>/dev/null \
        | awk '{print $3}' | sed 's/\.[0-9]*$//' \
        | sort | uniq -c | sort -rn
done
```

Expected (after source routing): each NIC shows **only its own
source IP** outbound. If `<NIC_A>` is showing both `<LOCAL_A>` and
`<LOCAL_B>` outbound packets, the source routing isn't applied —
or the mount predates it (TCP sockets cache routes; remount).

## 11.9 What we don't yet automate

CI doesn't and probably can't run any of this — we'd need the actual
storage cluster on the runner. The lab procedure is run manually
when a release candidate is being cut, and the results are recorded
in the release notes (e.g. "tested against an 8×IPv6 OceanStor
fabric — 16-stream throughput 636 MB/s").

If you have access to a comparable storage rig (OceanStor, NetApp,
PowerScale, Lustre w/ NFS gateway), please run §11.7 and post the
table to the issue tracker — multi-vendor numbers are valuable for
catching vendor-specific NFS quirks.

## 11.10 Caveats / known gotchas from the lab

- **OceanStor only supports IPv4 in production deployments** the
  vendor sells. The IPv6 fabric in the project lab is a customisation
  over the same hardware. Customer-facing test plans should cover
  IPv4 multipath as the primary scenario; IPv6 is what we've
  stress-tested at scale internally.
- **Server-side caching** can hide real throughput. Use offsets far
  enough into a multi-GiB testfile that the data isn't in any cache,
  AND `echo 3 > /proc/sys/vm/drop_caches` on the client between
  runs.
- **`iflag=direct` is essential** for these measurements — without
  it the kernel page cache absorbs the workload and you measure
  memory bandwidth, not the network path.
- **Don't trust mount options output blindly.** `mount` shows
  `addr=<SRV1>` (the primary) only — the multipath state lives in
  `/proc/enfs/<id>/path`. A single-line `mount` output does NOT
  mean only one transport is active.
