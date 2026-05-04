# 10. Testing and CI

> Prerequisites: [chapter 1](./01-architecture.md) (the Option B′
> build pipeline) and [chapter 8](./08-patch-series.md) (because CI
> applies the patch series for each target). This chapter focuses on
> the surface around the build, not on the unit-test code.

The project ships a small but deliberate CI surface: three
GitHub Actions workflows, three on-target test VMs, a tagged-release
flow, and a documented manual mount-test procedure. This chapter
maps each piece to the file on disk that defines it, explains the
choices that aren't obvious from reading the YAML alone, and is
honest about the failures that early releases shipped.

## 10.1 Workflow files and what each does

Three YAML files under [`.github/workflows/`](../../.github/workflows/):

| Workflow | File | Triggers | Jobs |
|---|---|---|---|
| `lint` | [`lint.yml`](../../.github/workflows/lint.yml) | push to `main`, PR to `main` | `shellcheck`, `markdownlint`, `make-help`, `secret-leak-guard` |
| `build` | [`build.yml`](../../.github/workflows/build.yml) | push to `main`, PR to `main` | `build-noble` (matrix: 6.8, 6.14), `build-resolute` (7.0), `build-arm64-native` (matrix: 6.8, 6.14), `build-deb` |
| `release` | [`release.yml`](../../.github/workflows/release.yml) | push of a `v*` tag | `release` (single job: build .deb + create GitHub Release) |

Total of ten status checks gate `main`: 4 lint + 5 build matrix
entries (3 amd64 + 2 arm64) + 1 .deb. They all must be green for a
PR merge to be accepted (branch protection — see §10.5).

### `lint.yml` — four jobs

- **`shellcheck`** runs `ludeeus/action-shellcheck@2.0.0` over
  `scripts/`. Severity `warning`; `SC1091` is suppressed because the
  developer-local `secrets/local-env.sh` is gitignored.
- **`markdownlint`** runs `DavidAnson/markdownlint-cli2-action@v23`
  over `README.md` and `docs/**/*.md` with the repo's
  `.markdownlint.json`.
- **`make-help`** runs `make help` to verify the Makefile parses.
- **`secret-leak-guard`** greps tracked files for private LAN IPs,
  hostnames, MAC addresses, and developer-VM passwords. Excludes
  `vendor/` (upstream sources) and the workflow file itself (which
  mentions the patterns it greps for). Belt-and-braces against the
  patterns ever landing in a public commit.

### `build.yml` — three matrices

The build matrix is structured around the runner OS and target
kernel availability, not around what *would* be elegant.

#### `build-noble` — 6.8 + 6.14 on `ubuntu-24.04`

The Ubuntu 24.04 ("noble") archive carries both `linux-headers-generic`
(6.8.0-x) and the HWE kernel `linux-headers-6.14.0-37-generic`. A
single `runs-on: ubuntu-24.04` runner can therefore cover both 6.8
and 6.14 with no container needed. The matrix has two entries:

```yaml
- target: ubuntu-6.8
  kver-prefix: "6.8.0"
  headers: "linux-headers-generic"
- target: ubuntu-6.14
  kver-prefix: "6.14.0"
  headers: "linux-headers-6.14.0-37-generic"
```

The `kver-prefix` step picks the actual installed `KVER` from
`/lib/modules/` (the kernel image and the headers can differ on
public runners as Ubuntu updates them).

6.11 is intentionally omitted from this matrix — the
[`Makefile`](../../Makefile) lists `ubuntu-6.11` as a supported
TARGET, but the corresponding `vendor/ubuntu-6.11/` and
`patches/ubuntu-6.11/` are not yet committed. Adding 6.11 is purely
mechanical (vendor MANIFEST + rebase the 20 patches) but has not
been done.

#### `build-resolute` — 7.0 inside an `ubuntu:26.04` container

Ubuntu 26.04 ("resolute") has no public GitHub Actions runner image
yet. The workaround:

```yaml
runs-on: ubuntu-24.04
container:
  image: ubuntu:26.04
```

The 24.04 runner provides the Docker daemon; the 26.04 container
provides the matching `linux-headers-generic` (a 7.0.x kernel
header package). Build steps run inside the container. This adds
~20 s to job startup compared to a native runner but is otherwise
transparent. When 26.04 ships a public runner image, this can switch
to native.

#### `build-arm64-native` — 6.8 + 6.14 on `ubuntu-24.04-arm`

GitHub Actions provides Ampere Altra (`ubuntu-24.04-arm`) runners
that build natively in ~2 minutes. The previous arrangement — a
QEMU+binfmt container running on amd64 hardware — took 20–50
minutes. The native runner saves wall time and proves the source
actually compiles for arm64.

Two important caveats baked into the workflow:

1. The runner is a sandboxed VM that does not allow loading
   foreign-built modules (it runs its own preinstalled kernel and
   refuses `modprobe` on arbitrary `.ko`s). The job builds the
   modules and verifies they're aarch64 ELF, but does *not*
   `modprobe enfs`. A separate on-target VM (`enfs-dev-24-arm64`,
   §10.7) handles real load testing.
2. The job *does* run `apt install ./enfs-dkms_*_all.deb` and
   `dkms status -m enfs`. The `.deb` is `Architecture: all` —
   DKMS does the per-arch compile on the target machine using
   installed `linux-headers-*`. This validates that the postinst
   `dh_dkms` substitution and `dkms add` / `dkms install` actually
   work on a real arm64 kernel with real arm64 headers.

#### `build-deb` — Architecture-all package

The `.deb` itself has no per-arch content (DKMS does the kernel-side
compile at install time). The job runs `dpkg-buildpackage -us -uc -b`
on a stock `ubuntu-24.04` runner. Lintian runs as part of
`dpkg-buildpackage`. The artefact is uploaded to the workflow run
for inspection.

#### What each job verifies, by `Kbuild` aggregate

[`Kbuild`](../../Kbuild) builds six modules (the `obj-m += ...`
lines): `sunrpc.ko`, `nfs.ko`, `fs/nfs/nfsv3.ko`,
`fs/lockd/lockd.ko`, `fs/nfs_common/nfs_acl.ko`, and
`fs/nfs/enfs/enfs.ko`. Each build job has a confirmation step:

```bash
for ko in src/sunrpc.ko src/nfs.ko src/fs/nfs/nfsv3.ko \
          src/fs/lockd/lockd.ko src/fs/nfs_common/nfs_acl.ko \
          src/fs/nfs/enfs/enfs.ko; do
    [ -f "$ko" ] || { echo "::error::missing $ko"; exit 1; }
    ls -lh "$ko"
done
```

The arm64 job adds an `file "$ko" | grep -q "ELF 64-bit LSB.*aarch64"`
check to make sure the cross-build didn't silently produce x86
objects.

### `release.yml`

Triggered by `push` of a `v*` tag (e.g. `git tag v0.1.5 && git push
origin v0.1.5`). One job:

1. Checkout, install build deps.
2. Cache `~/.cache/enfs-vendor` so subsequent releases don't re-fetch.
3. `dpkg-buildpackage -us -uc -b`.
4. Collect the produced `.deb` / `.changes` / `.buildinfo` into
   `dist/`.
5. `softprops/action-gh-release@v3` creates a GitHub Release with the
   files attached and a hand-templated body containing the support
   matrix.

The release body is not auto-generated — it includes a deliberate
table of "what's tested where" (see
[`release.yml`](../../.github/workflows/release.yml) lines 49-83)
and the `apt install ./enfs-dkms_*_all.deb` install snippet.
`generate_release_notes: true` adds the auto-changelog *under* the
hand-templated body.

## 10.2 Why a 26.04 container, not a 26.04 runner

GitHub-hosted runner images lag distro releases. At the time
`build.yml` was written, no `ubuntu-26.04` runner image existed.
`runs-on: ubuntu-24.04` + `container: image: ubuntu:26.04` keeps the
job working without waiting. When a public 26.04 runner ships, the
job becomes `runs-on: ubuntu-26.04` minus the `container:` block.

## 10.3 Why `ubuntu-24.04-arm` for arm64

Three options were considered: QEMU+binfmt on amd64 (20–50 min per
job — slow), self-hosted arm runners (no infra), or
`ubuntu-24.04-arm` Ampere Altra public runners (native, ~2 min). We
took the third.

The tradeoff: hosted arm runners are sandboxed VMs. They will not
load arbitrary kernel modules (their running kernel is GitHub's;
ours is foreign). Build coverage is full; on-target *load* coverage
is not. The `enfs-dev-24-arm64` test VM (§10.7) fills the gap.

## 10.4 Vendor materialisation from `linux-source-*` packages

`vendor/ubuntu-{6.8,6.14,7.0}/` is *not* committed verbatim. Each
target directory carries only `UPSTREAM-REVISION` (a shell-sourceable
pin file: `LINUX_SOURCE_PKG`) and `MANIFEST` (a list of paths to
copy). The actual stock kernel source files are materialised on
demand by [`scripts/fetch-vendor-ubuntu.sh`](../../scripts/fetch-vendor-ubuntu.sh)
from the `linux-source-X.Y.Z` apt package installed on the build
host.

> **Changed in #16 (2026-05):** previously this script `dget`'d the
> source package from `archive.ubuntu.com`. That made offline builds
> impossible and required all three `linux-source` packages to be
> reachable on a single distro (which they aren't). The script now
> reads from `/usr/src/${LINUX_SOURCE_PKG}.tar.{bz2,xz,gz}` only;
> no internet access is involved.

The fetch script:

1. Reads `LINUX_SOURCE_PKG` from the pin file. Errors out (with a
   migration hint) if it sees the legacy `SRC_PKG`/`ARCHIVE_URL`
   fields.
2. Locates `/usr/src/${LINUX_SOURCE_PKG}.tar.{bz2,xz,gz}`. If
   missing, errors with `apt install ${LINUX_SOURCE_PKG}` hint and
   exits non-zero.
3. Computes a cache key from the tarball SHA. If
   `vendor/$TARGET/.pin-stamp` matches the wanted stamp *and* a
   spot-check MANIFEST entry exists on disk, exits early.
4. Extracts the tarball into `$ENFS_VENDOR_CACHE` (default
   `~/.cache/enfs-vendor/`) — idempotent per-tarball-SHA.
5. Walks the MANIFEST and copies each entry into a sibling
   `vendor/$TARGET.fetching.$$/` directory.
6. Atomically renames the sibling over `vendor/$TARGET/`.
7. Stamps the pin into `vendor/$TARGET/.pin-stamp`.

### When the materialisation runs

There are two callers:

- **Developer workstation:** `make port` calls the fetcher for the
  target matching the developer's running kernel. Devs install
  `linux-source-X.Y.Z` for the target they're working on.

- **End-user .deb install (the load-bearing one):** As of #16 the
  `.deb` does **not** bundle `vendor/ubuntu-X.Y/` kernel trees.
  Instead the DKMS PRE_BUILD hook on the user's machine
  ([`scripts/dkms-pre-build.sh`](../../scripts/dkms-pre-build.sh))
  invokes the fetcher for the user's kernel target, which uses
  whichever `linux-source-X.Y.Z` package is installed there. The
  `.deb` `Depends: linux-source` so a sane default is pulled in
  automatically.

### HWE-kernel workaround (no binary `linux-source-*.tar.bz2`)

Ubuntu's `linux-hwe-X.Y` source packages **don't** produce a
`linux-source-X.Y.Z` binary deb (verified for `linux-hwe-6.14` on
24.04 noble — only `-headers`, `-tools`, `-cloud-tools` are built).
For HWE targets the workflow is:

```bash
# Enable deb-src (modern apt: deb822-format .sources file)
sudo sed -i 's|^Types: deb$|Types: deb deb-src|' \
    /etc/apt/sources.list.d/ubuntu.sources
sudo apt-get update

# Download + extract the HWE source via apt source
mkdir -p /tmp/hwe-src && cd /tmp/hwe-src
apt-get source linux-hwe-6.14

# Point the fetcher at the extracted tree
export ENFS_LINUX_SOURCE_TREE="$(realpath linux-hwe-*)"
make port TARGET=ubuntu-6.14
```

The `ENFS_LINUX_SOURCE_TREE` environment variable bypasses the
`/usr/src/<pkg>.tar.*` lookup and uses the supplied directory
directly. Set it (empty string) to fall back to the GA path. The
CI workflow ([`build.yml`](../../.github/workflows/build.yml))
implements this fallback automatically: it tries the binary
package first and falls back to `apt source` if missing.

This split solves the impossible-cross-distro requirement: the .deb
build host doesn't need any kernel source at all, and the user's
machine only needs source matching its own kernel.

Refer to [chapter 8](./08-patch-series.md) for the patch-application
step that runs *after* fetch, inside `make port`.

The cache strategy still applies: every build job in `build.yml`
and `release.yml` mounts `~/.cache/enfs-vendor` as an
`actions/cache@v4` step keyed by the hash of the relevant
`UPSTREAM-REVISION` file(s). First build of a new pin extracts
(~10–30 s); subsequent builds copy from the cache.

## 10.5 Branch + PR flow

- All work happens on a `feat/...` or `fix/...` branch. Direct
  commits to `main` are blocked by branch protection.
- A PR opens. The 10 status checks (lint x4 + build x6) run.
- All 10 must be green to merge. If any fails, the PR is blocked.
- `main` is also protected against force-push (GitHub branch
  protection setting). Rewriting history on `main` would invalidate
  every contributor's checkout and is reserved for emergencies (e.g.
  removing a leaked secret — see the secret-leak guard in §10.1).

CI runs on PRs against `main` and on every push to `main` (which,
under the protection rules, means "every merged PR"). The PR run
catches breakage before merge; the post-merge run catches
flake-induced false greens (rare, but it has happened — usually a
network blip during `apt-get update`).

## 10.6 Release flow

Tagging `v0.1.5` (for example) and pushing the tag fires
`release.yml`:

```bash
git tag v0.1.5
git push origin v0.1.5
```

The workflow builds the `.deb`, creates a GitHub Release at
`https://github.com/darrenstarr/huaweienfs/releases/tag/v0.1.5`,
attaches the artefacts, and renders the body. `generate_release_notes:
true` appends the auto-changelog under the hand-templated body.

Releases are deliberately a manual act — no `merge to main → auto
release`. Cutting a release means committing to a support claim, and
the operator should have looked at `git log` between the previous
tag and this one before pushing.

## 10.7 On-target test VMs

Three test VMs cover different arch / kernel combinations. Their
provisioning is documented in `secrets/` (developer-local,
gitignored — see chapter 1's note on the Option B′ split between
public docs and per-developer setup).

| VM | OS / kernel | What it tests |
|---|---|---|
| `enfs-dev` | Ubuntu 26.04, kernel 7.0 | end-to-end on the GA target — the canonical "is this release usable" VM |
| `enfs-dev-24` | Ubuntu 24.04, kernel 6.8 + HWE 6.14 | the older Ubuntu LTS lineup (one VM, two installable kernels) |
| `enfs-dev-24-arm64` | Ubuntu 24.04 arm64 (under QEMU emulation on the developer's amd64 build host) | the arm64 module-load gap that hosted runners can't cover |

Per-VM workflow (the developer-side loop, not CI):

```bash
make sync-vm           VM_HOST=...       # rsync src + scripts to the VM
make build-on-vm       VM_HOST=...       # ssh, run make modules
make smoke-on-vm       VM_HOST=...       # dkms-install, modprobe enfs, dmesg
```

The `enfs-dev-24-arm64` VM is the documented gotcha: hosted arm
runners refuse to `modprobe` foreign-built modules, so on-target
arm64 module-load testing has to happen on a QEMU-emulated VM under
the developer's control. The same VM proves DKMS install + modprobe
on the slowest path; if it works there, it works on real Ampere.

## 10.8 Releases that shipped CI-green and were unusable

This is the honest section. Three early tagged releases passed the
full CI matrix (all 10 checks green) and shipped `.deb`s that
*failed at install* on a real Ubuntu machine:

- **v0.1.1** — DKMS module compiled in the build environment but the
  postinst script failed in a way the build-deb job didn't surface.
- **v0.1.2** — fixed v0.1.1 but introduced a different postinst
  regression.
- **v0.1.3** — same shape of problem.

The lesson, stated plainly: **CI-green is necessary but not
sufficient.** The CI build runs `dpkg-buildpackage` to produce the
`.deb`, but at the time it did not run `apt install ./*.deb` on a
real machine. A `.deb` that builds is not a `.deb` that installs;
the postinst hooks (DKMS add / build / install) only run at install
time, and any breakage there isn't visible from the build artefact
alone.

**v0.1.4** was the first release that:

1. Built green (existing CI).
2. Was apt-installable on every supported target.
3. Module loaded after install + reboot.

The structural fixes since:

- The arm64 build job now runs `apt install ./enfs-dkms_*_all.deb`
  and `dkms status -m enfs` to validate the postinst on a real
  arm64 kernel. The amd64 build matrix should grow the same
  installer step (and the same on-target validation against
  installed `linux-headers-*`) — that's a known followup.
- The release-cut process gained a manual step: developer installs
  the candidate `.deb` on `enfs-dev` *before* pushing the tag.
  Catches the postinst-regression class of bug.

If you're cutting a release: don't rely on the CI green badge alone.
Install the candidate `.deb` on at least one real VM, reboot,
`modprobe enfs`, verify dmesg is clean, mount something. Only then
push the tag.

## 10.9 The manual mount-test procedure

End-to-end functional verification ("does multipath actually
multipath") is a manual procedure documented in
[`docs/user/04-operations.md`](../user/04-operations.md) and the e2e
topology under [`docs/e2e-test-topology.md`](../e2e-test-topology.md).
Short form:

1. Stand up two or more NFS server endpoints exporting the same
   share (the e2e topology uses LXC containers).
2. Mount with `vers=3,enfs_info=...,remoteaddrs=A~B~C,localaddrs=L`.
3. `cat /sys/kernel/sunrpc/xprt-switches/switch-*/xprt_switch_info`
   — look for `num_xprts: N` matching the number of `remoteaddrs`.
4. Run a multi-MiB `dd` from the mount while watching `tcpdump`.
   Each transport should see a roughly even fraction of the RPCs.
5. Drop the network on one server (`iptables -A INPUT -s <client>
   -j DROP` on the victim). I/O continues — failover (chapter 5)
   reroutes in-flight RPCs.
6. Restore the dropped server. pm_ping (chapter 4) re-detects it
   and the round-robin includes it again.

Run this before cutting a release that bumps the second or third
version digit. Not in CI because it requires multi-host network
setup hosted runners can't provide.

## 10.10 What's automated, what isn't, and what's pending

A short audit of the testing surface as of writing:

| Concern | Automated? | Where |
|---|---|---|
| Does it compile on amd64 6.8? | Yes | `build.yml :: build-noble[6.8]` |
| Does it compile on amd64 6.14? | Yes | `build.yml :: build-noble[6.14]` |
| Does it compile on amd64 7.0? | Yes | `build.yml :: build-resolute` |
| Does it compile on arm64 6.8? | Yes | `build.yml :: build-arm64-native[6.8]` |
| Does it compile on arm64 6.14? | Yes | `build.yml :: build-arm64-native[6.14]` |
| Does the `.deb` build? | Yes | `build.yml :: build-deb` |
| Does the `.deb` install on arm64 6.14? | Yes | `build.yml :: build-arm64-native` |
| Does the `.deb` install on amd64 (any version)? | No | manual on `enfs-dev` / `enfs-dev-24` |
| Does `modprobe enfs` succeed on arm64? | No (hosted runners won't load foreign modules) | manual on `enfs-dev-24-arm64` |
| Does multipath actually round-robin? | No | manual `tcpdump` / `xprt_switch_info` (§10.9) |
| Does failover actually fail over? | No | manual; iptables-drop one server (§10.9) |
| Are there secret leaks in tracked files? | Yes | `lint.yml :: secret-leak-guard` |
| Does shell pass shellcheck? | Yes | `lint.yml :: shellcheck` |
| Does Markdown pass markdownlint? | Yes | `lint.yml :: markdownlint` |
| Does Makefile parse? | Yes | `lint.yml :: make-help` |
| Do userspace unit tests pass? | Yes | `test.yml :: userspace-unit-tests` (§10.11) |

Pending, in rough priority order:

1. **amd64 install validation** — extend `build-noble` and
   `build-resolute` to `apt install ./enfs-dkms_*_all.deb` and run
   `dkms status -m enfs` after build. The arm64 job already does
   this; replicating it on amd64 closes the v0.1.1–v0.1.3 class of
   regression.
2. **6.11 target** — vendor MANIFEST + rebase the 20 patches.
   Mechanical work; not started.
3. **End-to-end functional test in CI** — would require multi-host
   network setup. Possible with `services:` containers in GitHub
   Actions but adds significant complexity. Currently manual.

The maturity message: this is a young project. The CI surface is
deliberate and growing, but it is not a substitute for a real
multi-host smoke test before each release. Treat the green badge as
"the thing built"; treat `enfs-dev` walking through §10.9 as "the
thing works."

## 10.11 Userspace unit tests (`tests/`, `make test`)

Started 2026-05-04. The C logic in
`vendor/openeuler/fs/nfs/enfs/*.c` is exercised in **userspace**
under [libcheck](https://libcheck.github.io/check/) — no kernel
build, no VM, no root. This complements the existing
[`enfs_test.c`](../../src/fs/nfs/enfs/enfs_test.c) KUnit stub
(which covers in-kernel integration); the userspace suite covers
pure logic where iteration speed and proper debug tools matter.

### Why a userspace layer at all?

- **Iteration is sub-second** instead of "sync→VM→modprobe→dmesg".
- `gdb`, `valgrind`, `clang -fsanitize=address`, `gcov` — all just
  work because the test binary is a normal userspace process.
- Acts as a **behavioral baseline** that any future port (a Rust
  rewrite, a refactor of the selection algorithm) must reproduce.

### Architecture

```text
tests/
├── kernel-shim/linux/        userspace fakes for kernel headers
├── kernel-shim/enfs_preempt.h force-included; preempts include
│                              guards for enfs internal headers
├── stubs/                    fakes for enfs-internal callees
├── unit/test_<module>.c      Check suites — one per source-under-test
├── fetch-linux-headers.sh    fetches upstream sunrpc headers
├── LINUX-HEADERS-MANIFEST    list of upstream files to fetch
├── UPSTREAM-REVISION         pinned Linux tag (default v6.14)
└── build/                    gitignored; cached headers + binaries
```

Two key tricks make this work:

1. **Kernel-API shim.** `tests/kernel-shim/linux/*.h` provides
   userspace fakes (`spinlock_t` → `pthread_mutex_t`, RCU → no-op,
   `kmalloc` → `malloc`, `list_head` → standard intrusive impl).
   The build prepends `-I tests/kernel-shim` so source files
   compile unmodified — no `#ifdef ENFS_USERSPACE_TESTS` markers.

2. **Include-guard preemption.** `enfs_preempt.h` is
   force-included (`-include`) and `#define`s the include guards
   for `enfs.h`, `enfs_config.h`, `linux/nfs_fs_sb.h`, and friends.
   When the source then `#include`s them, the guard fires and
   their content is skipped. We provide minimal substitutes for
   the symbols the source actually uses (~10 functions, ~3 structs).
   Avoids dragging in the entire NFS world (40+ transitive headers).

### Adding a test for a new enfs source file

1. Add fakes for that file's enfs-internal callees (with global
   control variables) in `tests/stubs/enfs_deps_stubs.c`.
2. Make sure `tests/kernel-shim/linux/` covers all kernel headers
   it `#include`s; add minimal shims as needed.
3. Write `tests/unit/test_<module>.c` as a Check suite.
4. In `tests/Makefile`, append the test name to `TESTS` and define
   `ENFS_SRC_<name>` and `STUBS_<name>`.
5. `make test`.

### CI

[`.github/workflows/test.yml`](../../.github/workflows/test.yml)
runs `make test` on `ubuntu-24.04` for every push and PR. The
upstream-headers fetch is cached on the `tests/UPSTREAM-REVISION`
hash, so subsequent runs are sub-second after the first.

This workflow is **independent** of `build.yml` — a broken kernel
build doesn't mask a test failure, and vice versa.

### Coverage status

`enfs_roundrobin.c` (~355 LOC, 22 functions) — **44 tests across 4
test cases**, with `make coverage` reporting:

| Metric | Coverage |
|---|---|
| Lines | 100.0% (153 / 153) |
| Functions | 100.0% (22 / 22) |
| Branches | 99.0% (99 / 100) |

The one uncovered branch direction is provably unreachable by
analysis (a monotonicity argument on the running minimum) and is
documented in `tests/unit/test_enfs_roundrobin.c` next to where
the corresponding test would land if the apparent production-code
bug is ever fixed.

`make coverage` produces an interactive HTML report at
`tests/build/coverage/index.html`. lcov 2.0+ required (apt:
`lcov`).

Phase 2 will extend coverage to `failover_path.c`, `pm_state.c`,
`enfs_multipath_parse.c`, and `dns_process.c`.

### Limitations

- **Single-threaded.** The shim's RCU and `smp_*` macros are
  no-ops. Concurrency tests would need a different approach
  (KUnit, or threaded tests with real pthreads — not currently
  scaffolded).
- **No real network.** Anything that calls into actual sunrpc
  transports cannot be unit-tested here. Use the smoke tests
  (§10.9) for that.
- **Shim drift.** If upstream Linux changes a struct field, the
  minimal shim doesn't notice; production module breaks at build
  time. Defense: append the affected header to
  `LINUX-HEADERS-MANIFEST` so the real layout gets used once a
  test depends on layout fidelity.
