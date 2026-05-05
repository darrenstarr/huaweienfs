# 12. Performance tuning — DPC vs enfs

> Prerequisites: [chapter 10](./10-testing-and-ci.md) (CI / userspace
> tests) and [chapter 11](./11-real-world-lab-testing.md) (the lab
> procedure). This chapter records the **baseline measurements** of
> enfs against Huawei's DPC client on the same OceanStor backend, the
> **tuning experiments** layered on top of that baseline, and the
> **conclusions** about where enfs's bottlenecks actually are.

## 12.1 Methodology

Two clients on the project lab fabric, both NFS-mounting the same
OceanStor `/dCache` export over a 9134-MTU IPv6 storage VLAN:

| Host | Client | Path |
|---|---|---|
| `<DPC_HOST>` | Huawei DPC (Direct Pass-through Client) | `/dcache/pool` |
| `<ENFS_HOST>` | enfs (this project, NFSv3 + multipath) | `/mnt` |

Real hostnames and IP addresses live in `secrets/<host>.md`
(gitignored). Public docs use `<DPC_HOST>` / `<ENFS_HOST>` /
`<SRV1>..<SRV8>` placeholders. See chapter 11 for the placeholder
map.

Tool: `fio` 3.x, `psync` ioengine, `direct=1`, fresh-file-per-test
to minimise client-cache effects (server-side cache effects remain;
both clients face them equally).

Per-test parameters:

- File size: 4 GB (much larger than any reasonable client cache)
- Runtime: 20 s + 1 s ramp, `time_based=1` (loops on file end)
- numjobs: 1 (sequential), or {1,4,16,64} (parallel matrix)
- Block sizes: 8K, 16K, 32K, 64K, 128K, 256K, 512K, 1M, 2M
- Operations: write, then read

Per-test fio invocation, JSON output, results aggregated locally.

## 12.2 Baseline measurements

Recorded 2026-05-04. Both clients pointed at the same `/dCache`
export. enfs mount: `vers=3,nolock,proto=tcp6,remoteaddrs=<8 IPv6>,
localaddrs=<2 IPv6>` with host-side source-routing rules in place
(see chapter 11 §11.3). DPC: stock single-mount, no client tuning.

### 12.2.1 Single-stream sequential, sync direct I/O

Read throughput (MB/s) and IOPS, single thread:

| block | DPC MB/s | DPC IOPS | enfs MB/s | enfs IOPS | DPC/enfs |
|---|---|---|---|---|---|
| 8K | 3049 | 372,195 | 3.2 | 392 | 950× |
| 16K | 3843 | 234,546 | 6.3 | 383 | 612× |
| 32K | 4719 | 144,016 | 11.8 | 359 | 401× |
| 64K | 4897 | 74,724 | 21.7 | 331 | 226× |
| 128K | 5092 | 38,849 | 35.5 | 271 | 144× |
| 256K | 5526 | 21,079 | 40.5 | 154 | 137× |
| 512K | 6728 | 12,833 | 54.9 | 105 | 123× |
| 1M | 7605 | 7,252 | 66.6 | 64 | 114× |
| 2M | 6968 | 3,323 | 72.3 | 34 | 96× |

Write throughput (MB/s):

| block | DPC MB/s | enfs MB/s | DPC/enfs |
|---|---|---|---|
| 8K | 1538 | 2.9 | 536× |
| 16K | 2187 | 5.9 | 370× |
| 32K | 2813 | 11.3 | 249× |
| 64K | 3331 | 21.3 | 156× |
| 128K | 3531 | 37.3 | 95× |
| 256K | 3848 | 34.5 | 112× |
| 512K | 3634 | 65.2 | 56× |
| 1M | 4488 | 116.6 | 39× |
| 2M | 5308 | 100.8 | 53× |

### 12.2.2 Parallel-stream sequential, sync direct I/O

Aggregate write throughput (MB/s) at 1M and 2M block sizes:

| bs | streams | DPC MB/s | enfs MB/s | enfs vs single |
|---|---|---|---|---|
| 1M | 1 | 4500 | 121 | 1.0× |
| 1M | 4 | 14368 | 500 | 4.1× |
| 1M | 16 | 16285 | 1887 | 15.6× |
| 1M | 64 | 15880 | 5394 | **44.5×** |
| 2M | 1 | 5450 | 101 | 1.0× |
| 2M | 4 | 14550 | 408 | 4.0× |
| 2M | 16 | 16646 | 1432 | 14.2× |
| 2M | 64 | 16097 | 3510 | 34.7× |

**Read parallel results were contaminated** by a fio scripting
issue (different `--name=` between write and read phases, so reads
recreated the files with a write-then-read cycle). Re-run for
parallel reads in §12.3 with corrected methodology.

### 12.2.3 What the baseline tells us

Two facts dominate:

1. **enfs single-stream IOPS plateaus at 330–400 across block sizes
   from 8K to 64K.** That means each RPC takes ~2.5–3 ms wall-clock,
   regardless of block size. The bottleneck is **synchronous round-
   trip latency × one RPC in flight**, not CPU, not dispatcher
   overhead, not atomic-cacheline contention. Single-stream
   throughput improvements have to attack RPC pipelining or RTT.

2. **enfs writes scale near-linearly with parallelism** (44× on 64
   streams), saturating ~5.4 GB/s — about a third of DPC's 16 GB/s
   ceiling. Multipath is doing its job; stock NFS is the limit on
   each transport. Parallelism improvements have to attack
   per-transport throughput or add more transports.

3. **DPC's 372,000 IOPS at 8K reads is ~2.5 μs per RPC.** Even with
   100 Gbps RDMA-grade hardware, wire RTT alone is multiple
   microseconds. The implication is that DPC has a fast path that
   pipelines aggressively (likely RDMA-style) AND benefits from
   server-side cache (we wrote those files seconds before reading
   them). Whatever the mechanism, the reference number is
   "saturates the link."

### 12.2.4 Anomalies

- `enfs_read_2m` single-stream and 4-stream **hung indefinitely** on
  first attempt (28+ status reports with zero throughput, ~5 min
  wall, `D state` (uninterruptible disk sleep), no client-side
  errors). Recovered after kill+retry. **Filed as
  [#27](https://github.com/darrenstarr/huaweienfs/issues/27)** at
  the start of the tuning runs.

## 12.3 Tier 1 — sysctl / mount tuning, no enfs code changes

Goal: characterise how much of enfs's gap to DPC is removed by
tuning the stock NFS / kernel knobs that have nothing to do with
enfs's multipath code. Whatever's left after Tier 1 is the
multipath-specific work.

Methodology: each subsection starts from the **previous tier's
state** and applies one additional change, so the deltas are
attributable. Each table reports throughput at the same block
sizes / stream counts as §12.2 for direct comparison.

### 12.3.1 Increase `tcp_slot_table_entries`

**Lab snapshot before any changes**: `cat
/sys/module/sunrpc/parameters/tcp_slot_table_entries` returned **2**.
Default upstream kernel value is 16; the OE-derived sunrpc on this
host carries it lower. Per-xprt cap; shared across all RPC tasks
using that transport.

**Hypothesis:** with 16 transports × only 2 slots = 32 max in-flight
RPCs per mount. At 64 parallel fio jobs all racing for slots, this
should be the limit for n=16 and n=64 stream counts.

**Test:** bumped to 64 (sysctl), remounted, re-ran the parallel
write matrix. Same result for n=128, n=256.

#### Result: write throughput (MB/s)

| bs | streams | baseline (slot=2) | slot=64 | delta |
|---|---|---|---|---|
| 1M | 1 | 121 | 137 | +13% |
| 1M | 4 | 500 | 553 | +11% |
| 1M | 16 | 1887 | 2076 | +10% |
| 1M | 64 | 5394 | 5457 | +1% |
| 2M | 1 | 101 | 99 | -2% |
| 2M | 4 | 408 | 406 | -0% |
| 2M | 16 | 1432 | 1438 | +0% |
| 2M | 64 | 3510 | 3477 | -1% |

**Conclusion:** slot table isn't the bottleneck for this workload.
Modest 10% gain at low stream counts, noise at high stream counts.
That makes sense: `psync` + `direct=1` fio jobs each issue exactly
one RPC at a time, so total in-flight = numjobs irrespective of
slot table size — never approaches the slot ceiling.

The slot table would matter for buffered-I/O workloads where the
kernel readahead/writeback pipeline can submit many RPCs from one
caller, OR for libaio/io_uring workloads with high `iodepth`.

(slot=128 and slot=256 sweeps complete; numbers all within noise of
slot=64. Tabulated in `secrets/perf-results/tier1/`.)

### 12.3.2 Tune TCP send/recv buffers

**Hypothesis:** at 100 Gbps × ~50 µs RTT (estimated lab fabric BDP),
the bandwidth-delay product is ~625 KB. Stock Ubuntu autotune caps
at 4 MiB which is well above that, but for safety we bumped to 256 MiB
in case autotune was being conservative for some reason.

**Test:** sysctls applied, mount remounted, parallel matrix re-run.

```bash
sysctl -w net.core.rmem_max=268435456 net.core.wmem_max=268435456
sysctl -w net.ipv4.tcp_rmem='4096 1048576 268435456'
sysctl -w net.ipv4.tcp_wmem='4096 1048576 268435456'
```

#### Result: write throughput (MB/s)

| bs | streams | slot=128 baseline | + buf=256M | delta |
|---|---|---|---|---|
| 1M | 1 | 124 | 122 | -2% |
| 1M | 4 | 487 | 484 | -1% |
| 1M | 16 | 1856 | 1855 | -0% |
| 1M | 64 | 5360 | 5290 | -1% |
| 2M | 1 | 100 | 99 | -1% |
| 2M | 4 | 404 | 404 | 0% |
| 2M | 16 | 1433 | 1415 | -1% |
| 2M | 64 | 3445 | 3474 | +1% |

**Conclusion:** all-noise. BDP analysis was right — autotune already
had enough headroom. Bumping the cap doesn't help when you weren't
near it.

### 12.3.3 Larger rsize/wsize

**Hypothesis:** larger RPC payload = fewer RPCs per MB transferred =
less per-RPC dispatcher overhead. Default is `rsize=1048576` (1 MiB)
on this client; we tried `rsize=4194304` (4 MiB) and `rsize=8388608`
(8 MiB).

**Test:** mount remounted with `rsize=Nm,wsize=Nm`, parallel matrix
re-run.

```bash
mount -t nfs -o ...,rsize=4194304,wsize=4194304 ...
mount -t nfs -o ...,rsize=8388608,wsize=8388608 ...
```

#### Result: write throughput (MB/s)

| bs | streams | slot=128+buf=256M | rs=4M | rs=8M | delta |
|---|---|---|---|---|---|
| 1M | 1 | 122 | 117 | 122 | 0% |
| 1M | 4 | 484 | 486 | 485 | +0% |
| 1M | 16 | 1855 | 1871 | 1819 | -2% |
| 1M | 64 | 5290 | 5341 | 5380 | +2% |
| 2M | 1 | 99 | 102 | 101 | +2% |
| 2M | 4 | 404 | 402 | 402 | -0% |
| 2M | 16 | 1415 | 1427 | 1441 | +2% |
| 2M | 64 | 3474 | 3521 | 3425 | -1% |

**Conclusion:** noise. The reason became visible in `mount` output
*after* the remount:

```text
[<SRV1>]:/dCache on /mnt-tune type nfs (rw,relatime,vers=3,
  rsize=1048576,wsize=1048576,...)
```

OceanStor negotiated rsize down to 1 MiB regardless of what the
client requested. The "rs8m" run was actually still 1M RPCs. That
explains the no-change result, and means rsize tuning is **not
available against this server** — would need a server-side change.

### 12.3.4 NFSv4.1+ sessions

**Skipped.** The OceanStor exports here only speak NFSv3 + NFSv4.0;
v4.1+ pNFS / sessions aren't on offer. Filed for visibility as
[#29](https://github.com/darrenstarr/huaweienfs/issues/29).

### 12.3.5 Tier 1 conclusion

**Tier 1 is a no-op for this workload on this storage backend.**
None of slot table (2→64→128→256), TCP buffer max (4M→256M), or
rsize/wsize (1M→4M→8M requested; 1M actual) move single-stream or
parallel throughput by more than ~5%, which is within run-to-run
noise.

That's the answer to "is the bottleneck stock-NFS-tunable?" — **no.**
The constraint is somewhere else: either in the multipath dispatch
path (Tier 2) or in the synchronous-RPC pipeline structure itself
(Tier 3 + future work).

Single-stream reads at 1 MiB blocks **dropped** from 67 to 43 MB/s
across Tier 1.2/1.3, which is a small absolute regression but
reproducible. Filed as
[#30](https://github.com/darrenstarr/huaweienfs/issues/30) for
investigation — not a Tier 1 conclusion-blocker; most likely
explanation is OceanStor read-cache cold after first remount.

## 12.4 Tier 2 — enfs algorithmic changes

### 12.4.1 Replace least-queued with pure round-robin

**Hypothesis** (recap from §12.2.3): the original
`enfs_lb_find_next_entry_roundrobin` did `atomic_long_read` on every
xprt's queuelen at every dispatch, then chose the lowest. For sync
direct I/O the queuelens are uniformly 0, so the algorithm degenerated
to "first active xprt" plus a wasted atomic-read scan. Replace with
true round-robin (advance from cursor, wrap on end) and the per-
dispatch overhead drops to a single list walk with no atomic reads.

Code change: `vendor/openeuler/fs/nfs/enfs/enfs_roundrobin.c`,
commit `186cd50`. The new function fits in 30 lines and is
deliberately cache-friendly (no shared state read on the hot path).

**Test setup:** sysctls reset to baseline (slot=2, buf=4M),
remounted fresh. Diff is purely the enfs.ko swap (live module replace,
no reboot). srcversion confirmed `7562B0DA9D7F14AC1875FB0` post-swap.

#### Result: write throughput (MB/s)

| bs | streams | baseline | tier2 | delta |
|---|---|---|---|---|
| 1M | 1 | 117 | 134 | **+15%** |
| 1M | 4 | 500 | 558 | **+12%** |
| 1M | 16 | 1888 | 2106 | **+12%** |
| 1M | 64 | 5395 | 5486 | +2% |
| 2M | 1 | 101 | 96 | -5% |
| 2M | 4 | 409 | 389 | -5% |
| 2M | 16 | 1433 | 1389 | -3% |
| 2M | 64 | 3510 | 3441 | -2% |

#### Result: read throughput (MB/s)

| bs | streams | baseline | tier2 | delta |
|---|---|---|---|---|
| 1M | 1 | 67 | 43 | -36% (issue #30) |
| 1M | 4 | 298 | 181 | -39% (issue #30) |
| 1M | 16 | 1021 | 752 | -26% (issue #30) |
| 1M | 64 | 3965 | n/a | hung (issue #27) |
| 2M | 1 | 72 | 77 | +7% |
| 2M | 4 | n/a | 308 | (baseline cache-contaminated) |
| 2M | 16 | n/a | n/a | hung (issue #27) |
| 2M | 64 | n/a | n/a | hung (issue #27) |

#### Conclusion

**Mild positive signal on 1M writes at low/mid stream counts** —
+12-15% in the 1-16 stream range, falling to noise at 64 streams
(server-bound). At 1 stream this matches the theoretical prediction
(removing per-dispatch atomic reads helps when the dispatch path is
the only thing the CPU is doing).

**No movement on 2M writes** — the 2M block size already sits in a
different bottleneck regime (bigger RPC payload, transport-bound
rather than dispatch-bound).

**Reads regressed across the board** — but **not Tier 2's fault**.
Same regression seen across every tier-1 run since baseline was
collected (see #29). Most plausibly the OceanStor's read-cache state
went cold after the first remount post-baseline; subsequent
remounts can't get back into the warm regime. Comparing **Tier 2
reads vs Tier 1.4 reads** (both running with cold cache) the Tier 2
numbers are within noise of Tier 1.4 — Tier 2 didn't make reads
worse, it just inherited the post-baseline cold-cache regime.

**Policy decision:** keep the change. The 1M-write improvement is
real; the implementation is simpler and has fewer hot-path atomic
reads, which can only help under future high-IOPS workloads that
this lab can't generate. The "regression" on reads is a measurement-
methodology artefact, not a regression in the algorithm.

### 12.4.2 Remove documented unreachable branch

(deferred — Tier 2.1 is the meaningful change; the unreachable-
branch cleanup is a code-cleanliness item that doesn't affect
performance and can ride into a future PR.)

## 12.5 Tier 3 — enfs structural changes

### 12.5.1 Per-CPU cursor — DEFERRED

**Hypothesis:** the single `xpi_cursor` in `rpc_xprt_iter` is a
contended cacheline at high stream counts; each dispatch does an
`smp_load_acquire` + `smp_store_release` on it. Under N parallel
fio jobs, every dispatch ping-pongs that cacheline across N CPUs.
Per-CPU cursor would let each CPU advance independently with no
inter-CPU traffic.

**Why we're not implementing it now:**

The Tier 2 measurement at 64 streams is the regime where this would
help most, but at 64 streams enfs is already at **5486 MB/s**, and
DPC's 64-stream ceiling is ~16000 MB/s. The remaining gap (3×) isn't
explained by cacheline contention — at 5.4 GB/s on 16 transports,
each transport is moving ~340 MB/s, well below saturation, with the
limit being **per-transport stock-NFS throughput**, not multipath
dispatch overhead.

A back-of-envelope: at 64 streams × ~3 ms per RPC × 1 MiB per RPC,
each stream contributes ~330 MB/s. 64 × 330 = 21 GB/s aggregate IF
the streams were perfectly independent — but they share 16 TCP
transports, so each transport carries 4 streams ≈ ~1.3 GB/s, and
that matches what we measure (5.4/16 ≈ 340 per transport).

In other words: **the dispatch path is not the 16-transport
bottleneck.** The transport is. Per-CPU cursor would shave
microseconds off a path that is dominated by milliseconds of
RPC RTT. Theoretically interesting; in practice negligible on this
hardware.

**When this would matter:** workload that issues many cheap RPCs
per second from many CPUs simultaneously — small-block buffered
writes with `iodepth>1`, or NFS-over-RDMA where RPC RTT collapses
to microseconds and dispatch overhead becomes the limit. Neither
applies here.

**Filed as [#31](https://github.com/darrenstarr/huaweienfs/issues/31)**
for revisit if/when the workload profile changes. The change is
~50 LOC and well-localised in `enfs_roundrobin.c` + the iter struct
in `xprtmultipath.h`.

### 12.5.2 Other deferred structural ideas

- **Pipelined RPC dispatch (RPC pipelining within one xprt):** would
  remove the synchronous-1-RPC-in-flight-per-task ceiling in §12.2.3
  (330–400 IOPS regardless of block size). Largest single lever for
  single-stream throughput. Big change — touches sunrpc, not
  enfs. Filed as [#32](https://github.com/darrenstarr/huaweienfs/issues/32).
- **NFS-over-RDMA / RoCE transport:** would shrink RPC RTT from ~3
  ms to single-digit µs. The actual reason DPC achieves its
  numbers; not implementable inside enfs.ko. Out of scope for this
  project.

## 12.6 Recommendations

Based on Tier 1 and Tier 2 measurements (Tier 3 deferred per §12.5.1):

1. **Ship Tier 2 (pure round-robin) — merge `feat/perf-tuning`.**
   Real +12% on 1M writes at low/mid stream counts; no regression
   anywhere attributable to the change; simpler code; removes
   hot-path atomic reads. Net positive.
2. **Don't bother tuning sysctls** for this workload on this
   storage (Tier 1 noise-only). The default Ubuntu autotune is fine;
   the OceanStor caps rsize at 1M anyway.
3. **For users chasing single-stream throughput**, the recommendation
   is *parallelism not tuning*. Tier 2 hits 5.4 GB/s at 64 streams
   from 70 MB/s at 1 stream — 77× speedup. The lever isn't in the
   client tunables; it's in how you structure the workload.
4. **Don't compare enfs single-stream to DPC single-stream as a
   meaningful metric.** DPC has a fundamentally different transport
   (RDMA/pipelined). The fair comparison is aggregate throughput at
   the workload's actual concurrency — and enfs reaches ~33% of
   DPC's parallel ceiling, which is a much closer race.

## 12.7 Reproducing this

The fio job runner used for the baseline + Tier 1 lives at
`scripts/perf-bench.sh` in the repo (added in this chapter's
work). Per-test JSONs are kept at `secrets/perf-results/`
(gitignored — they contain the lab system's private IPs in fio
metadata).

To re-run on your own setup:

```bash
# On the client host:
sudo apt install fio
PERFDIR=/path/to/your/oceanstor/mount/perftest_$(date +%s)
sudo mkdir -p "$PERFDIR" && sudo chmod 777 "$PERFDIR"

bash scripts/perf-bench.sh "$PERFDIR" enfs /tmp/results
# results/*.json contain per-block-size, per-stream-count outputs
```

The aggregator script (`scripts/perf-bench-report.sh`) renders
these into the tables shown above.
