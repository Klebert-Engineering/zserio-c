# Benchmark methodology

This document is the operational reference for the `perf` CI job:
how it measures, what it gates on, why per-host, and how to add a
new host. It is the place to look before changing thresholds or
adding hardware.

## Why per-host

Cross-host comparisons of decode/encode `ns/op` are nonsense:

- **ISA differs.** Earlier perf work measured apollo decode at
  ratio `0.71` on a local aarch64 dev box and `0.90` on the
  Synology x86-64 runner — same code, same workload, different
  ISAs and microarchitectures.
- **Microarchitecture differs.** Cache sizes, prefetchers,
  out-of-order width, branch predictor — all swing the relative
  cost of our decode vs. the C++ runtime's decode by tens of
  percent in either direction.
- **Noise floor differs.** A 16-core x86 desktop with 128 GB and
  isolated workload sits at well under 2 % run-to-run; a shared 2-
  core Synology Pentium D1508 sat at ≈ 14 % on the same code.

The `perf` job therefore compares each run **only against the same
host's previous baseline**. The set of `docs/benchmarks/perf.baseline.<host>.json`
files in the repository is the multi-host record; the runner's host
label is the key.

## Hosts and runner labels

| host label | role                                             | gating | runner image |
|------------|--------------------------------------------------|--------|--------------|
| `primary`  | per-commit perf gate (canonical x86_64 baseline) | strict | `ci/Dockerfile` (Linux self-hosted) |
| `pi5`      | nightly aarch64 reference (Cortex-A76)           | strict | `ci/Dockerfile` |
| `macmini`  | nightly Apple-microarch aarch64                  | strict | macOS self-hosted runner agent |
| `jetson`   | nightly automotive-relevant Tegra aarch64        | strict | `ci/Dockerfile` |

The `perf` CI job's `runs-on` selector is `[self-hosted, perf,
<host>]`. A host opts in by registering its runner with
`RUNNER_LABELS=perf,<host>` in `ci/.env` (see
`docs/ci-self-hosted.md`).

The matrix in `.github/workflows/ci.yml` enumerates which hosts
participate per-commit. To add a host, append its label to the
`host: [primary]` list and bring up its runner. The matrix entry
without a corresponding runner is a noisy red gate (jobs queue
forever), so add hardware first.

## Naming

`docs/benchmarks/perf.baseline.<host>.json` — one file per host.

Host identifiers must match `[a-z0-9][a-z0-9._-]{0,63}` so they're
stable filesystem keys. `primary`, `pi5`, `macmini`, `jetson`,
`synology-d1508` all qualify.

Historical baselines from retired hardware (e.g.
`perf.baseline.synology-d1508.json`) are kept in-tree for
traceability. They are not referenced by any active matrix entry.

## Thresholds

The `perf` CI step runs `tools/check_perf_regression.py
perf.json` with `PERF_HOST` set to the runner's host label. The
script:

- Seeds `perf.baseline.<host>.json` on first run (new host, or
  first run after a workload-set change). Exit code 0.
- On subsequent runs, fails when **any** workload's `zserio_c`
  encode_ns or decode_ns has regressed by more than
  `REGRESSION_PCT_FAIL` (default 15 %).
- Reports an informational `↓ rebaseline?` hint when a metric
  has *improved* by more than `REGRESSION_PCT_REBASELINE`
  (default 30 %), pointing at a stale baseline that should be
  bumped.

The thresholds are global, not per-host. If a future host runs
noisier than 15 % on legitimate no-op commits, the right response
is to **widen the threshold globally with a justification recorded
in the commit message**, not to silently rebaseline. Per-host
threshold overrides would let a noisy host hide real regressions
on quieter hosts.

The Synology Pentium D1508 ran above this threshold (≈ 14 % noise,
spurious 20–40 % regression marks on no-op commits — documented
in `commit b8c4109` / `cf8bc3d`). It is retired; the `primary` x86
desktop replaces it as the canonical x86_64 baseline.

## Adding a new host

1. **Decide the host label.** Lower-case, filesystem-safe, stable
   across hardware refreshes. E.g. `pi5` (not `raspberry-pi-5-rev-c`
   — that's a commit-message detail, not a stable identifier).

2. **Bring up the runner.** On the host (terminal-only):

   ```bash
   git clone https://github.com/ke-fritz/zserio-c-shadow.git /opt/zserio-ci
   cd /opt/zserio-ci/ci
   cp .env.example .env
   $EDITOR .env          # GITHUB_PAT=..., RUNNER_LABELS=perf,<host-label>
   docker compose up -d --build
   docker compose logs -f   # watch registration
   ```

   Verify the runner shows **Idle** under the repo's
   **Settings → Actions → Runners** with labels including
   `perf,<host-label>`.

3. **Add the matrix entry** in `.github/workflows/ci.yml`'s `perf`
   job:

   ```yaml
   matrix:
     host: [primary, <new-host-label>]
   ```

4. **First CI run on the new host** seeds
   `docs/benchmarks/perf.baseline.<host-label>.json`. The seed
   commit's message should record: hardware (CPU, RAM), runner
   image (`ci/Dockerfile` rev), kernel/distro, and noise-floor
   observation (best done by running the harness 5–10 times in
   a row on the same code and recording the spread).

5. **Update this document** if the host needs documentation
   (custom scheduling, resource isolation tricks, kernel
   parameters, ...).

## What is and isn't gated

- **Gated:** zserio-c's own `ns/op` per workload per host. A
  regression vs the host's previous baseline fails the build.
- **Reported, not gated:** the `cpp/c` ratio. Bringing this above
  1.0 is the Phase 6 optimisation work; the gate measures
  *progress*, not a threshold.
- **Reported, not gated:** cross-host comparison. The aggregation
  step in the commit-comment chain produces a side-by-side table
  when multiple hosts have run. Reviewers eyeball it; CI does not
  fail on it.

## Resource isolation

A perf runner should be a dedicated machine — no interactive
sessions, no kernel builds in the background, no `cron`-scheduled
backups overlapping with the runner's working hours. Cache
contention from a single competing process can swing decode
`ns/op` by 5–10 % easily.

For Docker-hosted runners on Linux:

- Single perf runner per host (don't run multiple `ci/runner-*`
  services on the same physical box if it's a perf host — the
  workflow `concurrency:` setting prevents parallel perf runs
  but doesn't prevent unrelated CI jobs sharing the same
  hardware).
- `cpuset` pinning is optional but documented per-host if used.
- `nice -n -5` on the runner agent is fine on hardware where
  the runner is the only user-mode workload.

For macOS / Jetson hosts: the host-specific notes go in this
section as they're added.

## Cross-references

- `tests/benchmarks/run_benchmark.sh` — the harness.
- `tools/check_perf_regression.py` — the per-host gate.
- `.github/workflows/ci.yml` § `perf` — the matrix.
- `docs/ci-self-hosted.md` — runner setup (Docker compose).
- `requirements/performance/REQ-PF-001.md` — the requirement.
