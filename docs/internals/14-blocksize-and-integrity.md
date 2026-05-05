# 14. NFSv4.1 multipath integrity & block-size characterisation

> Prerequisites: [chapter 11](./11-real-world-lab-testing.md) (lab),
> [chapter 12](./12-perf-tuning.md) (perf-tuning baseline),
> [chapter 13](./13-rdma-and-load-sharing.md) (load-sharing analysis).
> This chapter records the integrity-first verification of an
> experimental enfs patch that lifts the `cl_vers == 3` gate on the
> round-robin dispatcher, followed by a block-size optimum sweep and
> parallel-stream characterisation, all driven from the project lab
> against a Huawei OceanStor that recently gained NFSv4 + v4.1.

The headline result is unexpected and changes the project's
strategy:

- The patch is **safe** on this OceanStor — cross-host SHA-256
  verification across 9 block sizes (4 KiB through 1 MiB) found no
  corruption — but
- It **doesn't actually multipath v4.1 traffic** — the kernel's
  v4.1 session machinery binds RPCs to one connection regardless of
  the iter cursor enfs sets. enfs creates 16 xprts on the v4.1 mount
  and the path manager dutifully pings them all, but the wire
  traffic measured in `mountstats` only ever increments one xprt
  counter.
- **v4.1 single-stream reads are 6–8× faster than enfs+v3+RR**
  anyway, because v4.1 sessions provide RPC pipelining within one
  transport (the slot table). For single-stream workloads — the
  user's stated top priority — v4.1 sessions alone deliver a much
  bigger win than client-side multipath ever did, with no enfs
  involvement required.

What this means for the next phase of the project is in §14.7.

## 14.1 Methodology

### 14.1.1 The patch under test

`enfs.ko` previously contained three explicit `version == 4` gates
that disabled enfs's multipath logic on NFSv4 mounts. Two of them
are lifted by an experimental module param `enfs_v4_rr` (default
off — v3 behaviour is unchanged unless the operator opts in):

| File | Line | What it gates | Lifted? |
|---|---|---|---|
| `vendor/openeuler/fs/nfs/enfs/enfs_multipath.c` | 925 | Creation of the 16 multipath xprts at mount time | yes when `enfs_v4_rr=1` |
| `vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c` | 232 | Selection of the round-robin iter ops table | yes when `enfs_v4_rr=1` |
| `vendor/openeuler/fs/nfs/enfs/enfs_multipath.c` | 989 | Per-file shard routing (depends on the v3-only EXTEND op) | **no** — sharding stays disabled on v4 because the underlying probe doesn't exist |

The patch is intentionally minimal (~15 lines diff across
`enfs_multipath.{c,h}` and `enfs_roundrobin.c`) and is gated behind
the param so all v3 deployments are unaffected by default.

A second, larger change was needed to make the test setup work at
all: the project's DKMS package previously did not ship a
replacement `nfsv4.ko`, only `sunrpc/nfs/nfsv3/lockd/nfs_acl/enfs`.
Without a rebuilt `nfsv4.ko`, the stock one fails to load against
our DKMS-replaced sunrpc with CRC mismatches on `rpcauth_create`,
`xdr_stream_pos`, etc. The vendor source for `nfsv4.ko` is already
in the `vendor/ubuntu-7.0/MANIFEST`; the patch adds the obj-list to
the top-level Kbuild and registers the module in `dkms.conf.in`.

### 14.1.2 Why the integrity test exists at all

NFSv4.1 sessions are spec'd (RFC 5661) such that a session is
logically bound to one transport unless the client explicitly
issues `BIND_CONN_TO_SESSION` to add additional connections. Naively
round-robining v4.1 RPCs across 16 transports — without
BIND_CONN_TO_SESSION — risks corrupting session state on servers
that strictly enforce per-connection slot/sequence ownership.

The user's mandate for this work was: **catch any corruption now,
before we layer RPC pipelining on top.** Integrity was the
load-bearing test; perf measurements were only allowed to proceed
on a clean integrity result.

### 14.1.3 Lab setup

| Role | Host | Mount | Access |
|---|---|---|---|
| Writer | `<LAB_HOST_04>` | `/mnt-v3rr` (control), `/mnt-v41rr` (subject) | enfs.ko v0.1.6 + experimental patch, srcversion `CAA2BA31D0BE7324126E426`, `enfs_v4_rr=1` |
| Reader | `<LAB_HOST_03>` | `/mnt-verify` | stock NFS (no enfs.ko) |
| Storage | OceanStor `<SRV1>..<SRV8>` | `/dCache` | NFSv3 + NFSv4 + NFSv4.1 (sessions, no pNFS, no v4.2) |

Both hosts on the IPv6 storage VLAN (MTU 9134). All public
hostnames anonymised to `<LAB_HOST_*>` and `<SRVn>`; real values in
`secrets/`.

Writer mount opts (both v3 and v4.1):
`remoteaddrs=<8 IPv6 server>,localaddrs=<2 IPv6 client>,
rsize=1048576,wsize=1048576` plus `nolock` for v3 only.

`/proc/enfs/<server>/path` confirms 16 paths in `Normal/CONNECTED|BOUND`
state on the v3 mount immediately after mount; for v4.1 the same
16 paths appear too (the multi-xprt creation gate is lifted).

### 14.1.4 Tooling

- **fio** with `psync` engine, `direct=1`, and `verify=crc32c
  --verify_pattern=0xdeadbeef` for integrity.
- **sha256sum** for the cross-host check (independent of fio and
  not subject to per-RPC-only verification).
- **drop_caches** invoked on both hosts between write and read
  phases to force the read path to go to the wire instead of any
  client page cache.
- New scripts:
  - `scripts/perf-storage-health-check.sh` — pre-test #34 wobble
    gate
  - `scripts/perf-verify-bench.sh` — controller-driven cross-host
    integrity (writer + reader hosts as args, controller doesn't
    have to be either)
  - `scripts/perf-blocksize-sweep.sh` — single-stream sweep 4 KiB
    to 1 MiB
  - `scripts/perf-parallel-bench-v4.sh` — N-stream sweep at the
    chosen optimum

## 14.2 Storage health verification

Before any other test, ran `perf-storage-health-check.sh /mnt-tune
v3-baseline`:

```
observed write bandwidth: 116 MB/s
GREEN — storage healthy
```

The OceanStor wobble first observed in #34 has resolved. Re-checked
between phases; no degradation observed during this chapter's runs.

## 14.3 Data integrity verification — cross-host SHA-256

For each block size, the test wrote a 256 MiB file on the writer,
fsynced (implicit via `direct=1`), dropped page cache on both hosts,
re-read on the writer with `fio --verify_only`, then sha256summed
both the writer's and reader's view of the file and compared.

### 14.3.1 enfs + v3 + RR (control)

| bs | intra-host fio verify | cross-host SHA-256 |
|---|---|---|
| 4 KiB | PASS | PASS |
| 8 KiB | PASS | PASS |
| 16 KiB | PASS | PASS |
| 32 KiB | PASS | PASS |
| 64 KiB | PASS | PASS |
| 128 KiB | PASS | PASS |
| 256 KiB | PASS | PASS |
| 512 KiB | PASS | PASS |
| 1 MiB | PASS | PASS |

9 / 9 PASS. Confirms the test rig is functioning end-to-end.

### 14.3.2 enfs + v4.1 + RR (subject)

| bs | intra-host fio verify | cross-host SHA-256 |
|---|---|---|
| 4 KiB | PASS | PASS |
| 8 KiB | PASS | PASS |
| 16 KiB | PASS | PASS |
| 32 KiB | PASS | PASS |
| 64 KiB | PASS | PASS |
| 128 KiB | PASS | PASS |
| 256 KiB | PASS | PASS |
| 512 KiB | PASS | PASS |
| 1 MiB | PASS | PASS |

9 / 9 PASS. **No corruption observed** across any block size, on
either intra-host fio CRC verify or independent cross-host SHA-256
comparison from a reader host that didn't even have enfs.ko loaded.

### 14.3.3 Why this passed — the inconvenient explanation

Before celebrating, look at `/proc/self/mountstats` on the writer
during the test:

```
v3 mount:    16 xprt:  lines, each with ~548K sends — perfect
                                                       round-robin.
v4.1 mount:  1 xprt:  line with 280K sends — all traffic through
                                              one transport.
```

A delta test confirms it: a 128 MiB single-stream write (128 RPCs
expected at bs=1M) increments exactly **one** xprt counter by 140
sends; the other 15 xprts are quiet.

So the integrity test passed **because there was no actual
multi-connection v4.1 traffic to corrupt.** enfs.ko created the 16
xprts (gate 1 lifted), set the round-robin ops on the iter (gate 2
lifted), and the path manager pings each path every 30 s. But the
NFSv4.1 client's per-RPC dispatch path uses the session-bound
transport, not the iter cursor. The round-robin happens at a layer
the kernel's v4.1 client never consults for session work.

This is consistent with the architecture analysis in
[chapter 13 §13.4.1–§13.4.2](./13-rdma-and-load-sharing.md): v4.1
sessions tie RPCs to a single transport unless explicit
`BIND_CONN_TO_SESSION` extends the session to additional
connections. enfs.ko does no `BIND_CONN_TO_SESSION` work — neither
does mainline Linux without `nconnect=` mount-time hinting.

**What we can claim:** the patch is harmless on this server. The
v4.1 client doesn't crash, doesn't error, doesn't corrupt; it
quietly ignores the multipath setup. v3 behaviour is unchanged.

**What we cannot claim:** that v4.1 multipath actually works, or
that "enfs+v4.1+RR" would be safe on a server that strictly
enforces session-conn binding (RFC 5661 explicitly allows servers
to reject misbound RPCs). This OceanStor is permissive by virtue
of the binding never happening at all on this stack. That's not
portable.

## 14.4 Optimal block size — single-stream sweep

`perf-blocksize-sweep.sh`, `psync direct=1 numjobs=1 size=4G runtime=30 ramp=2`,
both writes and reads. The eff column is throughput vs the next-smaller bs (≈1.0 means
the bs doubling did not help; ≈2.0 means it doubled throughput).

### 14.4.1 enfs + v3 + RR (16-xprt round-robin actually active)

| bs | write MB/s | write IOPS | eff | read MB/s | read IOPS | eff |
|---|---|---|---|---|---|---|
| 4 KiB | 1.5 | 373 | — | 1.7 | 405 | — |
| 8 KiB | 3.0 | 369 | 1.98× | 3.2 | 396 | 1.96× |
| 16 KiB | 5.9 | 358 | 1.94× | 6.4 | 392 | 1.98× |
| 32 KiB | 11.3 | 345 | 1.93× | 11.7 | 358 | 1.83× |
| 64 KiB | 20.5 | 312 | 1.81× | 20.2 | 309 | 1.72× |
| 128 KiB | 38.7 | 295 | 1.89× | 33.3 | 254 | 1.65× |
| 256 KiB | 34.4 | 131 | 0.89× ⚠ | 40.9 | 156 | 1.23× |
| 512 KiB | 65.9 | 126 | 1.91× | 55.0 | 105 | 1.34× |
| 1 MiB | 123.8 | 118 | 1.88× | 73.6 | 70 | 1.34× |

The 256 KiB write outlier (eff < 1.0) reproduces the same ~330 IOPS
plateau pattern documented in chapter 12 §12.2.3. IOPS hover at
300–400 across most of the range; throughput grows roughly with
block size until the storage backend's per-RPC service time
asserts itself.

**Optimal block size for v3+RR**: writes are still climbing at
1 MiB (no plateau), reads knee around 256 KiB but 1 MiB extracts
~1.3× more throughput. **1 MiB is the recommended single value.**

### 14.4.2 enfs + v4.1 + RR (effectively single-xprt, sessions active)

| bs | write MB/s | write IOPS | eff | read MB/s | read IOPS | eff |
|---|---|---|---|---|---|---|
| 4 KiB | 2.5 | 610 | — | **11.4** | 2,781 | — |
| 8 KiB | 5.0 | 604 | 1.98× | **22.3** | 2,726 | 1.96× |
| 16 KiB | 13.3 | 809 | 2.68× | **42.4** | 2,591 | 1.90× |
| 32 KiB | 18.0 | 550 | 1.36× | **78.1** | 2,383 | 1.84× |
| 64 KiB | 32.1 | 489 | 1.78× | **133.3** | 2,034 | 1.71× |
| 128 KiB | 72.2 | 551 | 2.25× | **228.3** | 1,742 | 1.71× |
| 256 KiB | 39.4 | 150 | 0.55× ⚠ | **347.1** | 1,324 | 1.52× |
| 512 KiB | 71.8 | 137 | 1.82× | **479.9** | 915 | 1.38× |
| 1 MiB | 129.2 | 123 | 1.80× | **574.9** | 548 | 1.20× |

The 256 KiB write outlier shows up in v4.1 too, which suggests it's
a backend-side phenomenon and not a client-side artefact. Worth
noting in any v3-vs-v4.1 comparison.

The reads column is the headline. **At 1 MiB, v4.1 single-stream
reads run at 575 MB/s vs v3+RR's 73 MB/s — a 7.8× speedup.**
**At 4 KiB, v4.1 reads are 11.4 MB/s vs v3+RR's 1.7 MB/s — 6.7×.**
The IOPS column tells the same story: 2,781 IOPS at 4 KiB on v4.1
vs 405 on v3+RR — 6.9× more in-flight work per second from a single
sync-direct caller.

This is the v4.1 sessions effect, which is the exact lever issue
[#32](https://github.com/darrenstarr/huaweienfs/issues/32) describes:
sessions allow multiple RPCs in flight per session via the
slot table negotiated at `CREATE_SESSION` time. Stock NFSv3 has
no such facility and is forever stuck at 1-RPC-in-flight per task.

Notably, the v4.1 numbers above are achieved with enfs's
multipath round-robin doing **nothing measurable** — only one xprt
sees any traffic. The win is entirely from v4.1 sessions, not from
client-side multipath.

**Optimal block size for v4.1**: 1 MiB for both reads and writes;
v4.1 reads are still scaling at 1 MiB (the next bs would likely
show further gains if the OceanStor would negotiate a higher
rsize, which §12.3.3 confirmed it won't).

### 14.4.3 4 KiB-specific note

The plan flagged 4 KiB as worth filing an issue if pathological.
It is not — 4 KiB lands on the same IOPS plateau as 8 KiB and
behaves as expected for the synchronous-RPC ceiling on each
mount. No issue filed.

### 14.4.4 2 MiB explicitly dropped

Per direction, 2 MiB block size is excluded from this and all
subsequent measurements in this chapter. Chapter 12 §12.2.1 already
documented its inefficiency (the single-stream IOPS curve goes
backwards from 1 MiB to 2 MiB on writes); this chapter does not
re-litigate that.

## 14.5 Parallel performance at OPT_BS = 1 MiB

`perf-parallel-bench-v4.sh PERFDIR LABEL OUTDIR 1m` —
`numjobs={1,2,4,8,16,32,64}`, `psync direct=1 size=2G runtime=30
ramp=2`, group-reported.

### 14.5.1 enfs + v3 + RR

| streams | write MB/s | write per-stream | read MB/s | read per-stream |
|---|---|---|---|---|
| 1 | 121 | 121.1 | 43 | 43.4 |
| 2 | 245 | 122.4 | 87 | 43.5 |
| 4 | 491 | 122.8 | 201 | 50.2 |
| 8 | 973 | 121.6 | 423 | 52.9 |
| 16 | 1,868 | 116.7 | 880 | 55.0 |
| 32 | 3,369 | 105.3 | 1,805 | 56.4 |
| 64 | 5,439 | 85.0 | 3,616 | 56.5 |

Writes scale near-linearly to 64 streams (5.4 GB/s aggregate;
per-stream slowly dropping from 121 to 85 MB/s as transports
contend). Reads scale a little better than linear at first
(per-stream actually grows from 43 to 56 as parallelism increases —
likely server-side cache warmup) and saturate near 3.6 GB/s at 64
streams. Both curves are consistent with chapter 12 §12.2.2.

### 14.5.2 enfs + v4.1 + RR (effectively single-xprt)

| streams | write MB/s | write per-stream | read MB/s | read per-stream |
|---|---|---|---|---|
| 1 | 133 | 133.1 | 570 | 570.3 |
| 2 | 261 | 130.4 | 1,127 | 563.6 |
| 4 | 511 | 127.8 | 1,461 | 365.4 |
| 8 | 906 | 113.3 | 1,700 | 212.4 |
| 16 | 1,410 | 88.1 | 1,762 | 110.1 |
| 32 | 1,374 | 42.9 | n/a | n/a (#27 hang) |
| 64 | 1,379 | 21.5 | n/a | n/a (#27 hang) |

The shape is what you'd expect from a single-transport v4.1
session: aggregate throughput climbs to a per-transport ceiling
(~1.4 GB/s writes, ~1.7 GB/s reads at n=16) and then refuses to
grow even as more streams pile on. The per-stream column
collapses correspondingly.

`n=32` and `n=64` reads timed out at the 60-second wrapper —
[issue #27](https://github.com/darrenstarr/huaweienfs/issues/27)'s
high-parallelism multipath dispatcher stall, recurring in v4.1
single-xprt mode too. Filed an inline comment on #27 noting the
v4.1 reproduction; not opening a duplicate issue.

### 14.5.3 v3 vs v4.1, side by side

| metric | v3+RR (16 xprts) | v4.1 (1 xprt + sessions) | winner |
|---|---|---|---|
| 1-stream write MB/s | 121 | 133 | v4.1 (+10%) |
| 1-stream read MB/s | 43 | **570** | **v4.1 (13.3×)** |
| 64-stream write MB/s | **5,439** | 1,379 | **v3+RR (3.9×)** |
| 64-stream read MB/s | **3,616** | 1,762 (n=16) | **v3+RR (~2×)** |
| Aggregate ceiling | scales to 64 | hits per-xprt cap at 16 | v3+RR |

For the user's stated priority — **single-stream first, parallel
second** — v4.1 is the dramatic win, especially for reads.
For aggregate-throughput-at-high-parallelism deployments, v3+RR
remains the right answer until enfs gains real session-bound
multipath (which is a sunrpc-layer change, not an enfs-layer
change).

## 14.6 What this means for the project

### 14.6.1 v4.1 sessions are the headline lever, not enfs multipath

Chapter 12 §12.2.3 framed the problem as "enfs single-stream IOPS
plateaus at 330–400 because synchronous one-RPC-in-flight." The
solution was framed as "RPC pipelining within a single xprt"
(issue #32). What this chapter shows is that **sessions alone, no
custom code**, deliver that pipelining today on this OceanStor:
single-stream 4 KiB reads jump from 405 IOPS (v3+RR) to 2,781 IOPS
(v4.1) — a 6.9× speedup — with no enfs work, no DKMS change, no
sunrpc rewrite. Just `mount -o vers=4.1`.

The implication: most of issue #32's value is reachable by simply
deploying v4.1 against an OceanStor that supports it. The custom
sunrpc-pipelining work would still help for v3-only deployments and
for closing the rest of the gap to DPC, but it is no longer the
project's only lever for single-stream throughput.

### 14.6.2 v4.1 multipath needs more than the iter patch

This patch makes enfs *try* to multipath v4.1 traffic and the v4.1
client *quietly ignores* the attempt. The 16 xprts get created,
the path manager tracks them, mountstats show 16 → 1 collapse the
moment session-bound dispatch starts.

To get real v4.1 multipath, one of the following has to happen:

1. **`BIND_CONN_TO_SESSION` work in sunrpc.** Mainline doesn't do
   it; enfs would need to drive it for each of its 16 xprts after
   the session is established. Non-trivial — touches RPC-layer
   protocol code, not enfs's transport-iterator code.
2. **`nconnect` instead of remoteaddrs/localaddrs for v4.1.**
   Mainline `nconnect=N` opens N connections per session and does
   the BIND dance; enfs's mount-option syntax doesn't currently
   plumb to this. Would be a smaller change but loses the
   per-IP-address control that enfs's `remoteaddrs=` syntax gives.
3. **A pNFS layout-driven path.** Out of scope until the OceanStor
   exposes pNFS layouts (currently `pnfs=not configured` per
   chapter 13 §13.7).

### 14.6.3 The patch's actual shipping value

The `enfs_v4_rr` module param defaults to off and changes nothing
about v3 behaviour. With it on, v4.1 mounts:

- Get 16 xprts created (free for the path manager and for any
  future BIND_CONN_TO_SESSION wiring).
- Don't crash, don't corrupt, don't error.
- Don't actually multipath either — but they don't pretend to,
  and any code path that relies on the iter cursor for routing
  will now have 16 xprts to round-robin through if/when the
  session-binding piece lands.

**Recommended ship behaviour: keep the patch, keep the param
default off.** Document that `enfs_v4_rr=1` is preparation
plumbing for future v4.1 multipath, not a working multipath today.

## 14.7 Recommendations

1. **For new lab + customer deployments where the OceanStor speaks
   v4.1: prefer NFSv4.1 over NFSv3 for any single-stream-heavy
   workload.** The 6–8× single-stream read speedup is too large to
   leave on the table. enfs is not in the data path for v4.1
   single-stream wins — it's just sessions.
2. **For multi-stream / aggregate-throughput workloads: stay on
   enfs+v3+RR.** v3+RR scales to 5.4 GB/s aggregate writes; v4.1
   tops out at ~1.4 GB/s on a single connection.
3. **Reframe issue #32.** The headline framing ("RPC pipelining
   within one xprt is the missing lever") is correct in the
   abstract but partly obsolete on any v4.1 server: the slot table
   already does this. The remaining motivation for #32 is for
   v3-only servers and for the residual single-stream gap to DPC
   (575 MB/s v4.1 reads vs DPC's 7,605 MB/s @ 1 MiB — still a
   13× gap).
4. **File a follow-up to do BIND_CONN_TO_SESSION properly.** This
   is the work that would actually make v4.1 multipath deliver
   parallel-throughput wins on top of the single-stream wins.
   Cross-reference issue #32.
5. **Fold `nfsv4.ko` into the DKMS package permanently.** It was
   a blocker for this work and is non-optional for any future v4
   testing or customer deployment of the project's stack.

## 14.8 What we'd ask Huawei

(Mirrors the §12.7.5 list, focused on v4.1.)

1. **Confirm what `BIND_CONN_TO_SESSION` enforcement the OceanStor
   does.** Strict (rejects RPCs on non-bound conns), permissive
   (accepts and processes), or in-between? The integrity result in
   §14.3 is consistent with permissive (or never-tested), and
   that's load-bearing for whether the project can deliver v4.1
   multipath against an OceanStor at all.
2. **Confirm the session slot-table size offered.** A v4.1 server
   can advertise a slot table of any size; the OceanStor's offer
   determines how much in-flight pipelining is reachable per
   session per client. The single-stream 2,781 IOPS at 4 KiB
   suggests it's at least in the dozens, but the exact number
   would inform whether further gains are reachable per-session.
3. **Provide a server-side trace of one v4.1 mount with `nconnect=16`
   from a stock client.** Useful baseline for what proper session
   trunking looks like on the wire — comparison point for what we'd
   build into enfs.
4. **Comment on the 256 KiB write IOPS dip** (eff < 1.0 in §14.4.1
   and §14.4.2). It reproduces in both v3 and v4.1, which suggests
   server-side rather than client-side. Is there a known I/O size
   at which dCache changes its internal handling?
5. **Confirm whether enabling pNFS / NFSv4.2 is a config option on
   this OceanStor.** Both showed as unsupported (`pnfs=not
   configured`, v4.2 returned `Protocol not supported`); v4
   itself was a config flip per the conversation that prompted
   this work, so it's worth asking again at this scope.

## 14.9 Reproducing this

```bash
# On the writer host (with the patched enfs.ko + v4_rr=1 module loaded):
sudo apt install fio jq

# 0. Storage health gate.
bash scripts/perf-storage-health-check.sh /mnt-tune health

# 1. Cross-host integrity (controller-driven from anywhere with
#    SSH access to both hosts):
bash scripts/perf-verify-bench.sh \
    netadmin@<LAB_HOST_04> /mnt-v3rr/perftest_verify_v3rr \
    netadmin@<LAB_HOST_03> /mnt-verify/perftest_verify_v3rr \
    v3rr /tmp/perf-verify-v3rr
bash scripts/perf-verify-bench.sh \
    netadmin@<LAB_HOST_04> /mnt-v41rr/perftest_verify_v41rr \
    netadmin@<LAB_HOST_03> /mnt-verify/perftest_verify_v41rr \
    v41rr /tmp/perf-verify-v41rr

# 2. Block-size sweep (run on the writer host, separate per mount):
bash scripts/perf-blocksize-sweep.sh /mnt-v3rr/perftest_bsweep_v3rr v3rr_bsweep /tmp/perf-bsweep-v3rr
bash scripts/perf-blocksize-sweep.sh /mnt-v41rr/perftest_bsweep_v41rr v41rr_bsweep /tmp/perf-bsweep-v41rr

# 3. Parallel sweep at the chosen optimum (1 MiB here):
bash scripts/perf-parallel-bench-v4.sh /mnt-v3rr/perftest_par_v3rr v3rr_par /tmp/perf-par-v3rr 1m
bash scripts/perf-parallel-bench-v4.sh /mnt-v41rr/perftest_par_v41rr v41rr_par /tmp/perf-par-v41rr 1m
```

Per-test JSONs are kept under `/tmp/perf-*` on the writer; the
secrets/perf-results/ tree (gitignored) is the persistent home
for archived runs that contain real lab IPs in fio metadata.
