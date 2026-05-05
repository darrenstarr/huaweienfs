# enfs-dkms internals

A code-level walkthrough of how this DKMS package re-implements the
OpenEuler `enfs` multipath NFS client on top of the stock Ubuntu 26.04
LTS (kernel 7.0) NFS / SunRPC code, broken into chapters that can each
stand on their own. Aimed at a reader who has clone-checked the repo,
read [`README.md`](../../README.md), [`docs/ARCHITECTURE.md`](../ARCHITECTURE.md)
and [`docs/PORTING-NOTES.md`](../PORTING-NOTES.md), and now wants to
understand *the code*.

The chapters reference specific files and line numbers in this
checkout. Vendored Ubuntu sources under `vendor/ubuntu-7.0/` are lazy-
fetched; if absent, run

```bash
bash scripts/fetch-vendor-ubuntu.sh ubuntu-7.0
```

(takes ~30 s) before reading along.

## Table of contents

1. [Architecture overview](./01-architecture.md) — the three-module
   stack, the adapter-registration pattern, and where every file lives.
2. [Multipath at the SunRPC layer](./02-rpc-multipath.md) — the
   `rpc_xprt_switch`, the round-robin iterator, and the `cl_enfs` bit.
3. [The NFS mount flow](./03-nfs-mount-flow.md) — option parsing
   through `enfs_option` to a live multipath `rpc_clnt`.
4. [The `enfs.ko` core](./04-enfs-core.md) — module init/exit,
   per-clnt and per-xprt context blobs, the multipath manager.
5. [The path manager: liveness, state, and failover](./05-failover.md) —
   `pm_ping`, the state machine, `failover_handle`, retry policy.
6. *(reserved — EXTEND op and server-capability probing)*
7. [Locking, concurrency, and refcount hygiene](./07-locking-and-concurrency.md) —
   `xps_lock`, RCU, `xps_kref`, ops-pointer publish/retrieve.
8. [The patch series](./08-patch-series.md) — patch-by-patch
   reference for `patches/ubuntu-7.0/`.
9. [Compat shims](./09-compat-shims.md) — what
   `compat/enfs_compat.h` papers over and why.
10. *(reserved — live remount, `/proc/enfs/`, DNS rebind worker)*
11. [Real-world lab testing procedure](./11-real-world-lab-testing.md) —
    the on-iron load-sharing validation against a high-bandwidth
    NFS storage cluster.
12. [Performance tuning — DPC vs enfs](./12-perf-tuning.md) —
    baseline measurements, tier-by-tier tuning experiments, and
    where the bottlenecks actually live.
13. [NFS-over-RDMA and the future of client-side load sharing](./13-rdma-and-load-sharing.md) —
    forward-looking analysis of what RDMA + NFSv4.1 sessions
    would change about enfs's role.
14. [NFSv4.1 multipath integrity & block-size characterisation](./14-blocksize-and-integrity.md) —
    cross-host integrity verification of an experimental
    `enfs_v4_rr` patch + single-stream + parallel sweeps; the
    headline finding that v4.1 sessions deliver 6–8× single-stream
    reads vs enfs+v3+RR with no enfs work involved.

Chapters 6 and 10 are placeholders for follow-up work.
