# Stock-vs-eNFS diff report

Per-file diffs between OpenEuler's modified sources (vendored under
`vendor/openeuler/`) and:

- **OE-base** — the same file in upstream Linux 6.6, showing precisely
  what enfs adds.
- **Ubuntu 7.0** — the same file in the Ubuntu 26.04 kernel, the
  target this DKMS package builds against. This conflates "enfs adds
  X" with "Linux 6.6 → 7.0 changed Y", but it is the diff that matters
  operationally for the port.

See `SUMMARY.md` for the per-file index with line-count stats.

Files newly added by enfs (`fs/nfs/enfs/*`, the `*_adapter.*` pair,
`net/sunrpc/sunrpc_enfs_adapter.c`, `include/linux/sunrpc/sunrpc_enfs_adapter.h`)
have no upstream counterpart; their diffs against Ubuntu 7.0 are
"new file" and not interesting.

Regenerate after refreshing `vendor/openeuler/`:

```bash
scripts/generate-diff-report.sh \
    <BUILD_HOST>:/path/to/openeuler/OLK-6.6 \
    <BUILD_HOST>:/path/to/ubuntu/linux-7.0.0
```
