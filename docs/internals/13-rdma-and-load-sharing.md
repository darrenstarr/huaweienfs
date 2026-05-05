# 13. NFS-over-RDMA and the future of client-side load sharing

> Prerequisites: [chapter 2](./02-rpc-multipath.md) (the multipath
> dispatcher), [chapter 5](./05-failover.md) (path manager), and
> [chapter 12](./12-perf-tuning.md) (where the throughput limits
> actually live). This chapter is a forward-looking design analysis,
> not a record of measurements — the lab does not yet have the RDMA
> fabric reachable from a host that can run enfs, so the conclusions
> here are mechanical predictions backed by published xprtrdma
> behaviour, not benchmarks.

## 13.1 Why this question now

Two things changed in the lab on 2026-05-05:

1. NFSv4 and v4.1 were enabled on the OceanStor (they had been
   disabled by a recent reinstall). Probing from `<LAB_HOST_03>`
   on the NFS/SMB VLAN now confirms the server happily serves
   v3, v4.0, and v4.1 against `/dCache`; v4.2 is rejected
   (`Protocol not supported`), and v4.1 mounts come up with
   `pnfs=not configured` — sessions yes, layouts no.
2. NFS-over-RDMA was enabled on the storage side. RDMA traffic
   traverses a separate VLAN that `<LAB_HOST_03>` and
   `<LAB_HOST_04>` don't currently reach, so end-to-end mount
   probing of `proto=rdma` is deferred. The rest of this chapter
   is what we should expect *if* a project host gains an RDMA
   path to the OceanStor.

The question worth answering before any of that work is staffed:
**does RDMA simplify the load-sharing scenario that enfs.ko was
written to solve, and if so by how much?**

## 13.2 What enfs's load sharing currently solves

To evaluate "does RDMA replace this?" we need to be precise about
the four distinct jobs the multipath layer is doing today (chapter
2 covers the mechanics; this is the why):

1. **Per-task throughput ceiling.** A single sync-direct RPC takes
   ~3 ms wall-clock on the lab fabric (chapter 12 §12.2.3). One
   RPC in flight per task → ~330 IOPS per task irrespective of
   block size. enfs cannot break this ceiling for a single task
   over TCP because the ceiling is at the RPC layer, not the
   transport-count layer; but it does let *parallel* tasks each
   hit ~330 IOPS on their own xprt, so n tasks aggregate ≈
   n × per-task ceiling.
2. **Aggregate throughput / multi-NIC fan-out.** A single TCP
   connection on a 100 Gbps NIC moves hundreds of MB/s, not tens
   of GB/s, due to per-flow CPU and per-connection congestion
   limits. To use both client storage NICs and the server's eight
   addresses simultaneously, *something* has to spread RPCs over
   multiple TCP flows. Stock NFS doesn't do this; enfs.ko does.
3. **Failover / HA.** When a path goes down (NIC failure, switch
   reboot, server-side address taken offline), the dispatcher
   skips it and the path manager re-probes. The application sees
   no error.
4. **Server-side load distribution.** Even when the client side has
   plenty of bandwidth, spraying RPCs across the 8 OceanStor
   addresses spreads the work across the cluster's frontend nodes
   instead of pinning it to one.

Each of these motivations interacts differently with RDMA. The
short version of the rest of this chapter: RDMA largely
**eliminates** motivation 1, **partially** addresses motivation 2,
**does not affect** motivations 3 and 4, and changes how
motivation 2 should ideally be implemented.

## 13.3 What NFS-over-RDMA changes mechanically

`xprtrdma` (the RDMA transport for sunrpc) replaces TCP framing
with RPC-over-RDMA verbs:

- Connection setup goes through `rdma_cm`; the RPC payload moves
  via SEND/RECV plus zero-copy READ/WRITE for bulk transfers.
- Reliable in-order delivery is provided by the HCA (Reliable
  Connection queue pairs); the kernel does not retransmit, fragment,
  or maintain a TCP-equivalent state machine. There is no
  congestion-control feedback loop in software.
- Bulk transfers (read/write payloads above the inline threshold,
  default 1 KiB) use RDMA READ from the server / RDMA WRITE to the
  server, so the data path is one PCIe → NIC → wire → NIC → PCIe
  hop with no host-CPU touch on either side.
- Per-RPC wall time on a healthy datacenter RDMA fabric is
  **single-digit microseconds**, vs. the millisecond-scale of TCP
  here. The 1000× factor is real and reproducible across mature
  RDMA NFS deployments.
- A single RDMA queue pair can drive substantially closer to NIC
  line rate than a single TCP connection, because the per-flow CPU
  cost collapses and there is no software-side serialisation point.
  The exact number is hardware-dependent but the rule of thumb in
  the literature is "1 connection ~= many TCP connections" for
  bulk throughput.

Crucially for this project: `xprtrdma` is just another `rpc_xprt`
behind sunrpc's transport-switch interface. enfs.ko's
`enfs_xprt_iter_roundrobin` walks `xps_xprt_list` without caring
what the underlying transport is. **Nothing in enfs's architecture
needs to change for the existing dispatcher to work over RDMA**;
the question is whether it should.

## 13.4 Re-evaluating the four motivations under RDMA

### 13.4.1 Per-task throughput ceiling — RDMA mostly solves this

The 330-IOPS-per-task ceiling exists because synchronous one-RPC-
in-flight × ~3 ms RTT = 333 IOPS. Drop the RTT to 10 µs and the
arithmetic gives 100,000 IOPS per task — a 300× headroom. The
ceiling stops being the binding constraint for almost every
realistic single-stream workload; you become CPU-bound on the
client (issuing syscalls) or storage-bound on the server (actual
backend service time) long before the wire is the limit.

For this project: the headline gap to DPC in §12.2.3 (DPC at
372 K IOPS @ 8K reads, enfs at 392) is roughly the gap between
"DPC pipelines hundreds of RPCs over one TCP connection" and
"stock sunrpc does one." RDMA closes most of that gap by making
each RPC cheap enough that the pipelining matters less, even
without v4.1 sessions. With v4.1 sessions over RDMA you get both
levers at once.

**The project's biggest measured pain point disappears under
RDMA. Round-robin over many xprts is no longer needed for
single-stream throughput.**

### 13.4.2 Multi-NIC fan-out — RDMA helps but doesn't auto-solve

A single RDMA queue pair is bound to one HCA / one NIC. If the
client has two storage NICs, one queue pair uses one of them. To
saturate both NICs, you still need *something* that maintains two
queue pairs and distributes work across them.

There are three plausible answers, each with a different
implication for enfs:

- **HCA-level link aggregation (RoCE LAG / Mellanox bonding).**
  The two NICs are bonded at the verbs layer, traffic is sprayed
  by the HCA, and a single queue pair appears to use both NICs.
  Cleanest if available; replaces enfs's role for multi-NIC
  fan-out entirely. Vendor- and driver-specific; not universal.
- **NFSv4.1 client-side trunking** (`session_trunking`,
  `nconnect`). v4.1 allows multiple TCP/RDMA connections to be
  bound to one session, with the client load-balancing among
  them. Mainline Linux has partial support — `nconnect` is the
  practical lever today and works with both TCP and RDMA. This
  is the standards-track answer, and it largely overlaps with
  what enfs.ko currently does at the xprt-switch layer.
- **enfs-style xprt-switch multipath, but with RDMA xprts.**
  Keep the round-robin dispatcher; just have the underlying
  `rpc_xprt`s be RDMA queue pairs instead of TCP sockets. Works
  with no design change, but starts to feel redundant with
  `nconnect` on v4.1.

So RDMA **changes the right answer for fan-out from "16 TCP
xprts under enfs round-robin" to "1-2 RDMA xprts per NIC, ideally
under v4.1 nconnect."** enfs's mechanism still works, but it's
no longer the only or simplest tool.

### 13.4.3 Failover / HA — RDMA does not replace this

Path failure semantics are unchanged by RDMA:

- A NIC, switch, or server-address can still go offline.
- The application still expects ongoing I/O to drain via a
  surviving path with no visible error.
- Something on the client has to detect liveness, mark the path
  dead, and reroute new work. The mechanics differ slightly
  (RDMA flow steering and CM events vs. TCP keepalives) but the
  policy layer is identical.

enfs's path manager (chapter 5: `pm_ping`, the state machine,
`failover_handle`) is exactly this policy layer. **It remains
useful regardless of whether the underlying transport is TCP,
RDMA, or a mix.** RoCE LAG handles single-NIC failure for the
local hop but does not handle "the OceanStor address you were
talking to has gone away" — that's still a client-side path
decision.

### 13.4.4 Server-side load distribution — RDMA does not replace this

The OceanStor exposes 8 frontend addresses. Even if one client
NIC could drive the entire backend, talking to only one of those
8 addresses means one frontend node carries all the work, the
other seven sit idle, and the cluster's aggregate ceiling is
"one node's worth" instead of "eight nodes' worth."

Spreading RPCs across the 8 addresses is what enfs.ko gives you
today (and what `nconnect`-with-multiple-server-addrs gives in
the v4.1 model). **RDMA does not change this.** It's about
*server* parallelism, which the wire protocol can't answer.

## 13.5 NFSv4.1 sessions, with or without RDMA

Sessions deserve a separate note because they overlap with
enfs's value proposition the most.

A v4.1 session has a slot table negotiated at `CREATE_SESSION`
time. Each slot represents one in-flight RPC; the server promises
exactly-once execution per (slot, sequence) pair, so the client
can have N RPCs in flight on a single session without the
ordering problems v3 has. This *is* RPC pipelining within a
single transport — the lever that issue
[#32](https://github.com/darrenstarr/huaweienfs/issues/32) is
filed against, available natively without writing a custom
sunrpc layer.

Two configurations are interesting:

- **v4.1 + TCP, single session, slot table = N.** Single-stream
  throughput jumps because N RPCs are in flight per task instead
  of one. Goes a long way toward closing the §12.2.3 gap. enfs's
  existing 16-xprt round-robin dispatch becomes partly redundant
  here — one xprt with a wide slot table covers single-stream;
  enfs only adds value for multi-NIC and HA.
- **v4.1 + RDMA, single session, slot table = N.** Combines the
  per-RPC latency win of RDMA with the in-flight-window win of
  sessions. This is what most modern high-performance NFS
  deployments actually run. Single-stream IOPS becomes
  CPU-bound, not protocol-bound.

The OceanStor now negotiates v4.1 with sessions enabled
(`nfsv4: ... sessions, pnfs=not configured` in `mountstats`),
so this is testable today over TCP from `<LAB_HOST_03>` even
before RDMA is reachable.

## 13.6 What RDMA explicitly does **not** simplify

To be concrete about the limits:

- **Path liveness, failover, and recovery.** Same policy layer
  needed. RDMA CM events are a faster signal than TCP keepalives,
  but the decision-making lives above the transport.
- **Spreading load across multiple server frontend addresses.**
  Wire-layer concern. RDMA gives you a faster connection to
  whichever address you pick; it does not pick multiple addresses
  for you.
- **Cache consistency, locking, ACLs, security.** All NFS-protocol
  concerns; transport-agnostic.
- **The cold-cache / fresh-mount client state regression
  ([#30](https://github.com/darrenstarr/huaweienfs/issues/30)).**
  Whatever this turns out to be, it's at the NFS or sunrpc state
  layer, not the wire.
- **Misbehaviour of the OceanStor backend itself
  ([#34](https://github.com/darrenstarr/huaweienfs/issues/34)).**
  RDMA wouldn't have averted the wobble; it would just have
  delivered the wobble's symptoms faster.

## 13.7 What an RDMA-aware enfs would actually look like

Two design shapes are worth considering:

**Shape A — minimal change.** Keep the existing
`enfs_xprt_iter_roundrobin` and `pm_state` machinery. When the
mount is `proto=rdma`, the underlying `rpc_xprt`s are RDMA
queue pairs instead of TCP sockets, but the dispatcher walks them
identically. Use 1-2 xprts per client NIC × 8 server addresses =
16-32 xprts (about the same as today). No code changes; the
xprt-switch interface is transport-agnostic by design.

  - *Pros:* zero engineering cost; reuses every existing test
    and feature; preserves failover semantics unchanged.
  - *Cons:* probably more xprts than RDMA actually needs; misses
    the v4.1 session lever; perpetuates the "multipath as
    throughput hack" framing in a regime where it is no longer
    the throughput bottleneck.

**Shape B — v4.1-native.** Use one v4.1 session over RDMA per
NIC × server-address pair, lean on `nconnect` and the slot table
for in-session parallelism, and have enfs.ko's role narrow to
exactly two things: pick *which* address to send a session's
traffic to (load distribution), and steer around dead paths
(failover). This is closer to how stock mainline Linux is
evolving (nconnect + session trunking). enfs's xprt-iter would
become a path-selection layer above sessions, not an RPC-level
round-robin.

  - *Pros:* aligns with where mainline NFS is going; gets RDMA's
    full latency benefit; gets v4.1's in-session pipelining for
    free; smaller hot path.
  - *Cons:* meaningful rewrite — affects the dispatcher, the path
    manager, and the option-parsing layer; depends on stable v4.1
    nconnect/trunking support (mainline is partial today).

Shape A is the obvious first step (it's free). Shape B is the
direction the project would move if RDMA + v4.1 became the
preferred deployment.

## 13.8 Recommendation

The pithy answer is: **RDMA simplifies one of the four reasons
enfs exists (single-stream throughput), partly addresses a
second (multi-NIC fan-out), and does not touch the other two
(HA, server-side load distribution). It is not a replacement
for enfs; it changes which problems enfs is most uniquely good
at.**

Practical sequencing:

1. **First, exercise NFSv4.1 over TCP** from `<LAB_HOST_03>` now
   that the storage supports it. Re-run the §12.2 single-stream
   matrix against a v4.1 mount with a wide slot table
   (`nconnect=16` is the closest analog to enfs's 16-xprt
   spread). This tells us how much of the throughput gap to DPC
   v4.1+sessions closes for free, with no new code, no RDMA
   fabric, no custom sunrpc work. If sessions plus nconnect
   matches DPC at 8K-64K reads, the headline finding from
   chapter 12 changes substantially and so does the urgency of
   issue [#32](https://github.com/darrenstarr/huaweienfs/issues/32).
2. **Then, when an RDMA path is reachable from a project client,**
   re-run the same matrix at `proto=rdma` (Shape A: keep enfs as
   is, just change transport). Compare to the v4.1+TCP+nconnect
   numbers to isolate the RDMA contribution from the
   sessions/nconnect contribution.
3. **Defer Shape B (v4.1-native rewrite)** until the
   measurements above show a workload where the existing xprt-
   switch dispatcher is the binding cost. That hasn't been the
   case at any point in chapter 12's data; it is unlikely to be
   the case under v4.1 either.
4. **Keep the path manager / failover code as-is.** Nothing in
   the RDMA story changes its job description.
5. **Update issue [#29](https://github.com/darrenstarr/huaweienfs/issues/29)**
   from "v4 unavailable" to "v4 + v4.1 available, pNFS not
   configured, v4.2 rejected" with the probe results from
   §13.10.

## 13.9 What we still don't know

- **RDMA from a project client is untested.** The probes in this
  chapter all came from `<LAB_HOST_03>` on the NFS/SMB VLAN.
  Confirming the behaviour-prediction model above requires a
  client with verbs-stack + reachability to the storage on the
  RDMA VLAN.
- **OceanStor's pNFS posture.** `pnfs=not configured` could be a
  configuration option (analogous to v4 having been disabled
  before) or a fundamental absence in this product line. Worth
  asking the storage admin during the same conversation that
  enabled v4.
- **`nconnect` ceiling under sessions.** Mainline allows
  `nconnect` up to 16 by default (configurable up to 256). At
  what point does the slot-table window per session become the
  binding limit instead of `nconnect`? The DPC reference
  numbers (372K IOPS) suggest hundreds of slots per session;
  the OceanStor's session offer needs to be inspected (the slot
  count appears in `nfsstat -m` and in `/proc/self/mountstats`
  under the `nfsv4` line).
- **Whether the OceanStor's session slot offer is generous or
  miserly.** A v4.1 server can advertise a slot table of 1
  (effectively no pipelining). Worth checking before predicting
  any speedup from v4.1.

## 13.10 Probe data — 2026-05-05 from `<LAB_HOST_03>`

Server: `[<SRV1>]:/dCache`, IPv6 storage VLAN, MTU 9134.
Client: Ubuntu 26.04, kernel 7.0.0-15, stock `nfs-common` (no
enfs.ko involved — this was a clean probe of what the storage
serves).

`rpcinfo -T tcp6 <SRV1> nfs N`:

| version | server reply |
|---|---|
| 2 | not available (low/high = 3/4) |
| 3 | ready and waiting |
| 4 | ready and waiting |

`mount -t nfs -o vers=N,proto=tcp6 ...`:

| requested | actual | notes |
|---|---|---|
| `vers=3` | mounts as `vers=3` | `rsize=wsize=1048576`, `local_lock=all` (we requested `nolock`) |
| `vers=4` (auto) | mounts as **`vers=4.1`** | client picks the highest minor the server offers |
| `vers=4.0` | mounts as `vers=4.0` | |
| `vers=4.1` | mounts as `vers=4.1` | `mountstats` shows `sessions, pnfs=not configured` |
| `vers=4.2` | `Protocol not supported` | server caps at 4.1 |

Probed across `<SRV1>..<SRV8>` — identical reply on all 8.

`showmount -e <SRV1>`:

```text
Export list for <SRV1>:
/PMS_SPACE_PERFORMANCE_USER_            127.0.0.1
/dCache                                 *
/PMS_SPACE_PERFORMANCE_NAMESPACE_SYSTEM 127.0.0.1
```

NFS-over-RDMA was not testable from `<LAB_HOST_03>` because the
host has no route to the storage on the RDMA VLAN. Attempting
`mount -o proto=rdma` from this client returns
`Address family for hostname not supported` at the userspace
mount-helper layer, before any wire traffic is generated; this
is the expected failure mode for "no RDMA path" and not
diagnostic of the storage's RDMA posture.
