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

(populated after benchmark)

### 12.3.2 Tune TCP send/recv buffers

(populated after benchmark)

### 12.3.3 Larger rsize/wsize (if supported by OceanStor)

(populated after benchmark)

### 12.3.4 NFSv4.1+ sessions (if supported by OceanStor)

(populated after benchmark)

## 12.4 Tier 2 — enfs algorithmic changes

(populated after implementation + benchmark)

### 12.4.1 Replace least-queued with pure round-robin

### 12.4.2 Remove documented unreachable branch

## 12.5 Tier 3 — enfs structural changes

(populated after implementation + benchmark, only if Tiers 1+2
don't close enough of the gap)

### 12.5.1 Per-CPU cursor

## 12.6 Recommendations

(populated last, summarising what worked)

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
