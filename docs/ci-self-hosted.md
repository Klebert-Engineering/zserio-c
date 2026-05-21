# Self-hosted CI runners

GitHub-hosted minutes are a finite monthly quota. This document
describes the self-hosted runner setup that takes over when the
quota is exhausted (and, longer term, is the cheaper steady state).

Everything needed lives in [`ci/`](../ci/):

| File | Purpose |
| --- | --- |
| `ci/Dockerfile` | Builder image — Ubuntu 24.04 (the same OS GitHub-hosted `ubuntu-latest` runs, so the committed size/perf/coverage baselines and MISRA suppressions stay valid) + the full CI toolchain + `gh` + the GitHub Actions runner agent |
| `ci/entrypoint.sh` | Runner lifecycle — exchanges a PAT for a registration token at start, de-registers on stop |
| `ci/docker-compose.yml` | Three runner instances on one host, sharing the image |
| `ci/.env.example` | Template for the one secret the operator manages (a GitHub PAT) |

Most CI jobs target `runs-on: [self-hosted, linux, x64]` — any
runner with those base labels picks them up. The `perf` job is
the exception: it targets `[self-hosted, perf, <host>]` so it
routes only to runners that have explicitly opted in via
`RUNNER_LABELS=perf,<host>` in `ci/.env`. The full multi-host
policy is in `docs/benchmarks/methodology.md`; in short, the
canonical x86_64 perf box gets `RUNNER_LABELS=perf,primary`,
and additional hosts (Pi, Mac mini, Jetson, ...) come in as
`perf,pi5` / `perf,macmini` / `perf,jetson`.

The image bakes in python3 and JDK 17 (with `JAVA_HOME` set), so
the workflow uses no `actions/setup-python` or `actions/setup-java`
steps — those download toolchains at runtime and are redundant
against a purpose-built image. The `apt-get install` steps are
kept as fast no-ops.

## Hardware

Reference target: a Linux x86_64 server with 16 cores / 128 GB
RAM, always-on, dedicated. (Earlier history: a Synology DS3018xs
with Pentium D1508 / 2 cores / 32 GB. Retired — the noise floor
was too high for stable perf measurements; see
`docs/benchmarks/methodology.md` § Thresholds.)

The constraints that actually matter:

- **x86-64 for the `primary` perf box.** The `size` baseline is
  measured on x86_64; the `perf` baseline is per-host so a non-
  x86 host just seeds its own `perf.baseline.<host>.json` on
  first run. Non-`primary` perf hosts (Pi, Mac, Jetson) can be
  any architecture.
- **RAM** — the Java/ant jar builds want ~2 GB headroom; the LTO
  link of the `ndslive_tile` perf workload can spike to ~4 GB
  transient. 8 GB is the floor, more is comfortable. 32 GB is
  generous; the 128 GB on the reference Linux x86_64 server has
  abundant headroom.
- **Cores** — 2 is enough; jobs just serialise more. Three runner
  instances (the compose default) let GitHub dispatch the lighter
  jobs in parallel, roughly halving a full run's wall-clock. On a
  16-core box, duplicate the `runner-2` block in
  `ci/docker-compose.yml` up to `runner-6` for more parallelism;
  the image is shared, the resource overhead is small.
  *Historical note:* on the retired Synology, the DSM kernel
  shipped without CPU CFS quota support, so the compose file sets
  no `cpus:` limit (Docker would reject it). Memory cgroups work,
  so the `memory:` cap is kept. Both are no-ops on a Linux x86_64
  server with a normal kernel.
- **Disk** — the xvalidation and perf jobs generate large trees
  (`build-xvalidation/`, `build-benchmarks/`, 1500+ files for the
  ndslive schema). Budget ~20 GB of working space.

## One-time: create the PAT

The runners need a token to register themselves. Create a GitHub
Personal Access Token with permission to administer repository
runners:

- **Classic PAT**: `repo` scope.
- **Fine-grained PAT**: `Administration: Read and write` on the
  `zserio-c` repository.

GitHub → Settings → Developer settings → Personal access tokens.

The PAT is exchanged for a short-lived *registration* token at
container start (`entrypoint.sh`); it is never baked into the image
or the compose file. It lives only in `ci/.env`, which is gitignored.

## Deploying a runner (Linux x86_64 host, terminal-only)

The canonical path for the `primary` perf box and other Linux
runners is `docker compose` from a checkout of the repo on the
host. No GUI required.

```bash
git clone https://github.com/Klebert-Engineering/zserio-c.git /opt/zserio-ci
cd /opt/zserio-ci/ci

cp .env.example .env
$EDITOR .env       # GITHUB_PAT=<your PAT>; GITHUB_REPO already defaults
                   # to Klebert-Engineering/zserio-c. Set it explicitly
                   # to ke-fritz/zserio-c-shadow if registering against
                   # the legacy shadow mirror instead.
                   #
                   # On the primary perf box, also set:
                   #   RUNNER_LABELS=perf,primary
                   # Other perf hosts use perf,<their-label> (pi5 / macmini /
                   # jetson, ...). Non-perf hosts leave RUNNER_LABELS unset.

docker compose up -d --build   # ~5 min first time (toolchain + runner agent)
docker compose logs -f          # tail to watch registration
```

Within ~30 s of containers starting, three runners (default
`zserio-ci-1/2/3`) appear under **Settings → Actions → Runners**
showing **Idle**. To run more concurrent CI jobs on a beefier host
(e.g. the 16-core / 128 GB primary), duplicate the `runner-2`
block in `ci/docker-compose.yml` up to `runner-6` before the first
`docker compose up`.

### Historical: Synology Container Manager GUI

The earlier reference host was a Synology DS3018xs; the deployment
flow there was the Container Manager → Project UI rather than
direct `docker compose` CLI. Documented for the record — the
Synology is retired in favour of a dedicated Linux x86_64 host
(better noise floor for perf measurements). If you need the GUI
flow, the steps are: File Station upload of `ci/`, create `ci/.env`
next to `docker-compose.yml` (paste the PAT and any
`RUNNER_LABELS`), then Container Manager → Project → Create with
the folder containing `docker-compose.yml` and click through
build + start.
## Updating after a `ci/` change

When `ci/Dockerfile`, `ci/docker-compose.yml`, or `ci/entrypoint.sh`
changes, the running project needs a rebuild. From the runner host:

```bash
cd /opt/zserio-ci
git pull                              # keep ci/.env in place — it's gitignored
cd ci
docker compose up -d --build          # stops, rebuilds, restarts in one step
docker compose logs -f                # confirm re-registration
```

`entrypoint.sh` re-registers each runner on start (`--replace` keeps
the same names), so within ~30 s they're back to **Idle**.

## Operations

- **Logs**: `docker logs -f zserio-ci-runner-1` shows registration +
  each job's runner-side output. `docker compose logs -f` tails all
  three.
- **Restart**: `docker compose -f ci/docker-compose.yml restart`.
  The runner re-registers cleanly each time.
- **Stop / remove**: `docker compose -f ci/docker-compose.yml down`.
  `entrypoint.sh` traps the stop signal and de-registers each runner
  so it doesn't linger as an offline entry in the repo's runner list.
- **Bump the runner agent**: GitHub deprecates old runner releases
  periodically. Edit `RUNNER_VERSION` in `ci/Dockerfile`, then
  rebuild (see "Updating" above).
- **Disk / `_work`**: the runner's `_work` directory lives in each
  container's writable layer (no named volume — see the compose
  file's header comment for why). It's reset whenever a container is
  recreated, which happens on every rebuild; no manual cleanup
  needed. Cross-run caching of the upstream zserio jars is handled
  by `actions/cache` in the workflow, independent of `_work`.

## Security

Self-hosted runners on a **private** repository are safe — only
collaborators can push code that runs on them.

If `zserio-c` is ever made **public**, this changes: a fork pull
request could run arbitrary code on the runner host. Before going
public, either disable self-hosted runners for fork PRs or restrict
them under **Settings → Actions → Fork pull request workflows**. The runner containers run as a non-root `runner` user
with passwordless sudo scoped to the container — a compromised job
is contained to the container, but the container still shares the
host kernel, so the public-repo caveat stands.
