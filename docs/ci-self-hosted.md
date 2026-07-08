# Self-hosted CI runners

GitHub-hosted minutes are a finite monthly quota. This document
describes the self-hosted runner setup that takes over when the
quota is exhausted (and, longer term, is the cheaper steady state).

Everything needed lives in [`ci/`](../ci/):

| File | Purpose |
| --- | --- |
| `ci/Dockerfile` | Builder image — Ubuntu 24.04 (the same OS GitHub-hosted `ubuntu-latest` runs, so the committed size/perf/coverage baselines and MISRA suppressions stay valid) + the full CI toolchain + `gh` + the GitHub Actions runner agent |
| `ci/entrypoint.sh` | Runner lifecycle — exchanges a PAT for a registration token at start, de-registers on stop, runs the agent with `--disableupdate` (see [Keeping the runner agent current](#keeping-the-runner-agent-current)) |
| `ci/docker-compose.yml` | Three runner instances on one host, sharing the image |
| `ci/.env.example` | Template for the one secret the operator manages (a GitHub PAT) |
| `ci/refresh-runner.sh` | Zero-touch agent upgrade — rebuilds the image when a newer runner release exists (replaces the fragile in-container self-update) |
| `ci/systemd/zserio-runner-refresh.{service,timer}` | Weekly trigger for `refresh-runner.sh` so the agent stays current unattended |

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
RAM, always-on, dedicated.

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
- **Cores** — 2 is enough; jobs just serialize more. Three runner
  instances (the compose default) let GitHub dispatch the lighter
  jobs in parallel, roughly halving a full run's wall-clock. On a
  16-core box, duplicate the `runner-2` block in
  `ci/docker-compose.yml` up to `runner-6` for more parallelism;
  the image is shared, the resource overhead is small.
- **Disk** — the xvalidation and perf jobs generate large trees
  (`build-xvalidation/`, `build-benchmarks/`, 1500+ files for the
  ndslive schema). Budget ~20 GB of working space.

## Scope: repo runner vs org runner

A self-hosted runner is bound to exactly one target at registration
time. `entrypoint.sh` supports two scopes; pick one per deployment:

| Scope | When to use | `ci/.env` |
|---|---|---|
| **Repo** | The legacy `ke-fritz/zserio-c-shadow` mirror (a personal account, not under an org), or any single-repo deployment. | `GITHUB_REPO=owner/name` |
| **Org**  | The canonical `Klebert-Engineering/zserio-c` deployment, and any future repo in that org. One runner pool serves every repo in the org's runner groups it belongs to. | `GITHUB_ORG=org-name` |

Set **exactly one** of `GITHUB_REPO` or `GITHUB_ORG` per `.env` —
the entrypoint refuses both-set and neither-set configurations.

## One-time: create the PAT

The runners need a token to register themselves. Permissions depend
on the scope:

- **Repo scope:**
  - Classic PAT: `repo` scope.
  - Fine-grained PAT: Resource owner = target repo's owner;
    Repository → **Administration: Read and write** on the target repo.
- **Org scope:**
  - Classic PAT: `admin:org` scope.
  - Fine-grained PAT: Resource owner = the org; Organization →
    **Self-hosted runners: Read and write**.

GitHub → Settings → Developer settings → Personal access tokens.

The PAT is exchanged for a short-lived *registration* token at
container start (`entrypoint.sh`); it is never baked into the image
or the compose file. It lives only in `ci/.env`, which is gitignored.

## Deploying a repo-scoped runner (e.g. the shadow mirror)

The canonical path for the `primary` perf box and other Linux
runners is `docker compose` from a checkout of the repo on the
host. No GUI required.

```bash
git clone https://github.com/Klebert-Engineering/zserio-c.git /opt/zserio-ci
cd /opt/zserio-ci/ci

cp .env.example .env
$EDITOR .env       # GITHUB_PAT=<your PAT>
                   # Set GITHUB_REPO=ke-fritz/zserio-c-shadow (or any
                   # owner/name) and comment out GITHUB_ORG.
                   #
                   # On the primary perf box, also set:
                   #   RUNNER_LABELS=perf,primary
                   # Other perf hosts use perf,<their-label> (pi5 / macmini /
                   # jetson, ...). Non-perf hosts leave RUNNER_LABELS unset.

docker compose up -d --build   # ~5 min first time (toolchain + runner agent)
docker compose logs -f          # tail to watch registration
```

Within ~30 s of containers starting, three runners (default
`zserio-ci-1/2/3`) appear under **Repo → Settings → Actions →
Runners** showing **Idle**. To run more concurrent CI jobs on a
beefier host (e.g. the 16-core / 128 GB primary), duplicate the
`runner-2` block in `ci/docker-compose.yml` up to `runner-6` before
the first `docker compose up`.

## Deploying an org-scoped runner (Klebert-Engineering)

Org-scoped runners live in **org-level runner groups** and serve any
repo the group is enabled for. The default group ("Default") is
available to every repo in the org — fine for now with one repo;
narrow it later via Org → Settings → Actions → Runner groups if you
add lightweight projects that shouldn't be allowed to grab heavy
perf runners.

If the same host already hosts a repo-scoped deployment (e.g. the
shadow runners), the org deployment runs **alongside** it as a
separate compose project. Distinct `-p` project name + distinct
`RUNNER_NAME_PREFIX` keep the runner names from colliding on GitHub
and the container names from colliding in Docker:

```bash
# Pick a directory that is NOT the existing /opt/zserio-ci checkout —
# the two deployments need separate ci/.env files.
git clone https://github.com/Klebert-Engineering/zserio-c.git /opt/zserio-ci-org
cd /opt/zserio-ci-org/ci

cp .env.example .env
$EDITOR .env       # GITHUB_PAT=<org-scoped PAT, see "One-time" above>
                   # GITHUB_ORG=Klebert-Engineering
                   # (comment out GITHUB_REPO)
                   #
                   # Required to avoid collisions when the same host
                   # also hosts shadow runners:
                   #   RUNNER_NAME_PREFIX=ke-org-zserio-ci
                   #
                   # If this is the primary perf box, also set:
                   #   RUNNER_LABELS=perf,primary

# -p namespaces the compose project so it doesn't clash with the
# repo-scoped "zserio-ci" project also running on this host.
docker compose -p zserio-ci-org up -d --build
docker compose -p zserio-ci-org logs -f
```

Within ~30 s the new runners appear under **Org → Settings →
Actions → Runners** (not the repo's runners page — that only lists
repo-scoped runners). They show **Idle** and a label set of
`self-hosted, linux, x64` plus whatever `RUNNER_LABELS` added. Open
a PR or push to a branch on `Klebert-Engineering/zserio-c`; the
jobs' `runs-on` labels match either scope identically, so the org
runners pick them up without any workflow-side change.

### Operating two compose projects on one host

The two deployments are fully independent — each has its own
`.env`, its own runner names, and its own set of containers:

```bash
# Status / restart / shutdown — pass -p to target the right project:
docker compose -p zserio-ci      ps         # shadow runners
docker compose -p zserio-ci-org  ps         # org runners
docker compose -p zserio-ci-org  restart
docker compose -p zserio-ci-org  down
```

The Docker image (`zserio-ci-runner:latest`) is the same for both;
the first project to build it caches it for the second. Memory caps
are per-container, so three org runners + three shadow runners =
six × `memory: 8g` budget on the host. On the 128 GB primary that's
trivial; on a smaller box, drop a runner or two from each compose
file.

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

## Keeping the runner agent current

The runner agent's built-in **self-update is disabled** — the entrypoint
runs it with `--disableupdate`. In-container self-update is fatal: the
agent downloads a new release, then deletes and replaces `bin/` +
`externals/` via a *detached* process that must outlive the listener
restart to finish the swap. In a container that process dies when the
entrypoint exits on the update, so `bin/Runner.Listener` is left deleted
and the container crash-loops with **exit 127**. It is reliably fatal on
a slow disk (a Raspberry Pi SD card takes ~60 s to copy the new agent
into place — a wide window for the teardown to interrupt it) and only
*intermittently* survivable on a fast SSD. This is the documented reason
GitHub's own actions-runner-controller and the major community images
disable auto-update too.

So the agent version is **pinned in the image** (`RUNNER_VERSION` in
`ci/Dockerfile`) and upgraded by rebuilding — at build time, where a slow
or flaky link just means a slower build, never a half-applied swap. That
means the image must be refreshed periodically, because GitHub eventually
refuses jobs from a runner more than a few releases behind the minimum.

`ci/refresh-runner.sh` automates that to zero-touch. It queries the
latest `actions/runner` release, and **only when the pinned version is
behind** it bumps `RUNNER_VERSION`, rebuilds, and recreates the runners:

```bash
cd /opt/zserio-ci-org/ci
COMPOSE_PROJECT=zserio-ci-org ./refresh-runner.sh   # -p target; omit if only one deployment
```

Install the systemd timer to run it weekly, unattended:

```bash
sudo cp ci/systemd/zserio-runner-refresh.* /etc/systemd/system/
sudo $EDITOR /etc/systemd/system/zserio-runner-refresh.service   # set WorkingDirectory +
                                                                 # ExecStart (+ COMPOSE_PROJECT)
sudo systemctl daemon-reload
sudo systemctl enable --now zserio-runner-refresh.timer
systemctl list-timers zserio-runner-refresh.timer                # confirm next run
```

Recreating aborts a job that is mid-flight, so the timer fires at a quiet
hour (Sunday 04:00). On a host with two deployments (org + shadow),
install renamed copies of the unit pair, one per deployment, each with
its own `WorkingDirectory` and `COMPOSE_PROJECT`.

`refresh-runner.sh` edits the `RUNNER_VERSION` line in the host's
`Dockerfile`; on a git checkout that is a local modification, so pull
`ci/` updates with `git pull --autostash` (the timer keeps the local pin
at or ahead of upstream, so there is normally nothing to reconcile).

## Operations

- **Logs**: `docker logs -f zserio-ci-runner-1` shows registration +
  each job's runner-side output. `docker compose logs -f` tails all
  three.
- **Restart**: `docker compose -f ci/docker-compose.yml restart`.
  The runner re-registers cleanly each time.
- **Stop / remove**: `docker compose -f ci/docker-compose.yml down`.
  `entrypoint.sh` traps the stop signal and de-registers each runner
  so it doesn't linger as an offline entry in the repo's runner list.
- **Bump the runner agent**: automated — `ci/refresh-runner.sh` (weekly
  via the systemd timer) tracks the latest release, since the agent's own
  self-update is disabled. See [Keeping the runner agent current](#keeping-the-runner-agent-current).
  To force it now: `./refresh-runner.sh`.
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
