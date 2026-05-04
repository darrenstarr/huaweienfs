# enfs userspace unit tests

Tests for enfs C logic that **run outside the kernel**. The same
`vendor/openeuler/fs/nfs/enfs/*.c` files that get compiled into the
DKMS module are compiled here against a userspace shim of the kernel
API and exercised with [libcheck](https://libcheck.github.io/check/).

## Why

- **Iteration speed.** A test cycle is `make check` (sub-second after
  the first build), not `sync to VM → modprobe → dmesg → repeat`.
- **Real debug tools work.** `gdb`, `valgrind --tool=memcheck`,
  `clang -fsanitize=address`, `gcov` — all available because we're
  in a normal userspace process.
- **Future-proof against rewrites.** A behavioral baseline that any
  future port (Rust, etc.) must reproduce.

This is **not** a replacement for the in-kernel KUnit stub at
`src/fs/nfs/enfs/enfs_test.c`; KUnit covers integration with real
kernel infrastructure, this suite covers pure logic.

## Run

```bash
# Prerequisites (Ubuntu/Debian):
sudo apt install build-essential check libsubunit-dev pkg-config curl

# From the repo root:
make test

# Or from this directory:
cd tests && make check
```

First run downloads any upstream Linux headers listed in
`LINUX-HEADERS-MANIFEST` (currently empty — see "Architecture" below).

## Architecture

```
tests/
├── kernel-shim/linux/   our userspace fakes for kernel headers
│                        (spinlock_t → pthread_mutex_t, RCU → no-op,
│                         kmalloc → malloc, list_head → standard impl)
├── stubs/               C-side fakes for enfs-internal callees
│                        (pm_get_path_state, etc.) — controllable from
│                        tests via globals
├── unit/                Check test runners — one per source-under-test
├── fetch-linux-headers.sh   downloads real upstream sunrpc headers
├── LINUX-HEADERS-MANIFEST   list of headers to fetch (one per line)
├── UPSTREAM-REVISION    pinned Linux tag (default v6.14)
└── build/               gitignored; cached headers + binaries
```

### Why a shim and not real kernel headers?

For pure-logic tests, we care about the *fields* enfs touches, not the
exact byte layout of the structs. Pulling real `linux/sunrpc/clnt.h`
into userspace transitively requires faking 15+ infrastructure headers
(`linux/socket.h`, `net/ipv6.h`, `asm/signal.h`, `linux/lwq.h`, ...)
— a lot of work for no test-value gain. Minimal shim definitions are
faster to write, easier to read, and equally valid for testing
selection algorithms.

The fetcher infrastructure exists (and is wired into the build) so
that *future* tests that need real layouts (e.g. wire-format codec
tests in `exten_call.c`) can opt in by appending to the manifest.

### Why `-Dstatic=`?

The most interesting selection functions in enfs are `static`. We
compile the source-under-test with `-Dstatic=` to expose them for
direct testing. This is a well-known idiom and keeps us from having
to add `#ifdef ENFS_USERSPACE_TESTS` markers to the production source.

## Adding a test for a new enfs source file

1. Add fakes (if needed) for any enfs-internal functions that file
   calls into `stubs/enfs_deps_stubs.c`, with global variables that
   tests can twiddle to control behavior.
2. Make sure `kernel-shim/linux/` covers all kernel headers that file
   `#include`s. If it doesn't, add a minimal shim header (look at
   existing ones for the style).
3. Write `unit/test_<module>.c` as a Check suite (see
   `unit/test_enfs_roundrobin.c` for the pattern).
4. In `Makefile`:
   - Append the test name to `TESTS`.
   - Define `ENFS_SRC_test_<module> := $(ENFS_SRC_ROOT)/<module>.c`.
   - Define `STUBS_test_<module> := ...` (which stub files to link).
5. `make check`.

## Coverage

```bash
sudo apt install lcov     # one-time
make coverage             # from repo root, or `make coverage` in tests/
```

Output:

- `tests/build/coverage/index.html` — interactive lcov report (open in
  a browser; click into a file to see per-line and per-branch hits).
- Terminal summary printed at end of run.

Current coverage of `enfs_roundrobin.c` (44 tests across 4 test cases):

| Metric | Coverage | Note |
|---|---|---|
| Lines | **100.0%** (153 / 153) | every executable line hit |
| Functions | **100.0%** (22 / 22) | every function called |
| Branches | **99.0%** (99 / 100) | one branch is provably unreachable; documented inline |

The single uncovered branch is in `enfs_lb_find_next_entry_roundrobin`
at the `optimal_queuelen < min_xprt_queuelen` clause. Static analysis
shows it can never fire (`min_xprt_queuelen` is monotonically
non-increasing while `optimal_queuelen` is set from a then-current min),
suggesting a copy-paste bug in the production code. We don't modify
`vendor/openeuler/` to "fix" it; the test file documents the analysis
where a future test would naturally land if the production code is
corrected.

### Coverage in CI

`.github/workflows/test.yml` runs `make test` on every push/PR.
Coverage isn't enforced as a gate yet — adding it in a follow-up
once we have more modules covered (a single-module ratio isn't a
useful overall metric).

## Limitations and gotchas

- **Single-threaded.** RCU and `smp_*` macros are no-ops in the shim.
  Tests of locking behavior need a different approach (KUnit, or
  threaded tests with real pthreads — not currently scaffolded).
- **No real network.** Anything that calls into actual sunrpc transports
  cannot be unit-tested here. Use the smoke tests
  (`make smoke-on-vm`) for that.
- **Shim drift.** If upstream Linux changes a struct field, the shim
  doesn't know. Tests still pass; the production module breaks at
  build time. The right defense is to add the header to the manifest
  (real layout) once a test depends on layout fidelity.
