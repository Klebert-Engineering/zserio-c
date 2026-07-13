# Self-hosted CI runners (perf hosts)

Almost all CI runs on GitHub-hosted `ubuntu-latest` runners — see
`.github/workflows/ci.yml`. Self-hosted runners exist for exactly
one job: `perf`, the performance benchmark (REQ-PF-001), which
needs dedicated, always-on, otherwise-idle hardware so that
measurements are reproducible. Virtualized GitHub-hosted runners
have noisy neighbors and varying hardware generations; a perf gate
on them would be a coin flip.

**Security policy (enforced in the workflows, not just settings):**
no job that triggers on `pull_request` may target a self-hosted
runner. A fork PR would otherwise execute arbitrary code on the
runner host. The `perf` job is restricted to
`github.event_name == 'push'` on `Klebert-Engineering/zserio-c`,
so it runs only for commits that a maintainer has already merged
or pushed. See [Security](#security) for the defense-in-depth
settings that back this up.

Everything needed lives in [`ci/`](../ci/):

| File | Purpose |
| --- | --- |
| `ci/Dockerfile` | Builder image — Ubuntu 24.04 (the same OS GitHub-hosted `ubuntu-latest` runs, so the committed perf baselines stay valid) + the full CI toolchain + `gh` + the GitHub Actions runner agent |
| `ci/entrypoint.sh` | Runner lifecycle — exchanges a PAT for a registration token at start, de-registers on stop, runs the agent with `--disableupdate` (see [Keeping the runner agent current](#keeping-the-runner-agent-current)) |
| `ci/docker-compose.yml` | Three runner instances on one host, sharing the image |
| `ci/.env.example` | Template for the one secret the operator manages (a GitHub PAT) |
| `ci/refresh-runner.sh` | Zero-touch agent upgrade — rebuilds the image when a newer runner release exists (replaces the fragile in-container self-update) |
| `ci/systemd/zserio-runner-refresh.{service,timer}` | Weekly trigger for `refresh-runner.sh` so the agent stays current unattended |

The `perf` job targets `[self-hosted, perf, <host>]`, so it routes
only to runners that have explicitly opted in via
`RUNNER_LABELS=perf,<host>` in `ci/.env`. The full multi-host
policy is in `docs/benchmarks/methodology.md`; in short, the
canonical x86_64 perf box gets `RUNNER_LABELS=perf,primary`, and
additional hosts (Pi, Mac mini, Jetson, ...) come in as
`perf,pi4` / `perf,macmini` / `perf,jetson`.

The image bakes in python3 and JDK 17 (with `JAVA_HOME` set), so
the `perf` job uses no `actions/setup-python` or
`actions/setup-java` steps — those download toolchains at runtime
and are redundant against a purpose-built image. (The
GitHub-hosted jobs that build the upstream jars do use
`actions/setup-java`.) The `apt-get install` steps are kept as
fast no-ops.

## Hardware

Perf hosts are also the published measurement environments — their
specs are part of the benchmark methodology
(`docs/benchmarks/methodology.md`), not an implementation detail.

The constraints that matter:

- **x86-64 for the `primary` perf box.** The `perf` baseline is
  per-host, so a non-x86 host just seeds its own
  `perf.baseline.<host>.json` on first run. Non-`primary` perf
  hosts (Pi, Mac, Jetson) can be any architecture.
- **RAM** — the Java/ant jar builds want ~2 GB headroom; the LTO
  link of the largest benchmark workload can spike to ~4 GB
  transient. 8 GB is the floor, more is comfortable.
- **Cores** — 2 is enough; jobs just serialize more. Three runner
  instances (the compose default) let GitHub dispatch queued jobs
  in parallel; on a many-core box, duplicate the `runner-2` block
  in `ci/docker-compose.yml` for more parallelism — the image is
  shared, the resource overhead is small.
- **Disk** — the benchmark harness generates large trees
  (`build-benchmarks/`). Budget ~20 GB of working space.
- **Idle** — a perf host must not run other workloads while a
  measurement is in flight. The workflow serializes perf runs per
  host via a concurrency group, but it cannot see load from
  outside CI.

## Scope: repo runner vs org runner

A self-hosted runner is bound to exactly one target at registration
time. `entrypoint.sh` supports two scopes; pick one per deployment:

| Scope | When to use | `ci/.env` |
|---|---|---|
| **Repo** | Binds the runner to a single repository. Preferred for public repos — the runner cannot be reached by any other repo. | `GITHUB_REPO=owner/name` |
| **Org**  | One runner pool serves every repo the runner group is enabled for. Only appropriate when every repo in the group is trusted and the group is explicitly restricted. | `GITHUB_ORG=org-name` |

Set **exactly one** of `GITHUB_REPO` or `GITHUB_ORG` per `.env` —
the entrypoint refuses both-set and neither-set configurations.
For the public `zserio-c` deployment, prefer repo scope, or an
org-level runner group restricted to `zserio-c` only (Org →
Settings → Actions → Runner groups) — never the org-default group
that serves all repositories.

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
Prefer the fine-grained PAT — it limits the blast radius if the
token leaks.

The PAT is exchanged for a short-lived *registration* token at
container start (`entrypoint.sh`); it is never baked into the image
or the compose file. It lives only in `ci/.env`, which is gitignored.

## Deploying a perf runner

The canonical path is `docker compose` from a checkout of the repo
on the host. No GUI required.

```bash
git clone https://github.com/Klebert-Engineering/zserio-c.git /opt/zserio-ci
cd /opt/zserio-ci/ci

cp .env.example .env
$EDITOR .env       # GITHUB_PAT=<your PAT>
                   # GITHUB_REPO=Klebert-Engineering/zserio-c
                   # (or GITHUB_ORG with a restricted runner group)
                   #
                   # On the primary perf box:
                   #   RUNNER_LABELS=perf,primary
                   # Other perf hosts use perf,<their-label>
                   # (pi4 / macmini / jetson, ...).

docker compose up -d --build   # ~5 min first time (toolchain + runner agent)
docker compose logs -f          # tail to watch registration
```

Within ~30 s of containers starting, the runners appear under
**Settings → Actions → Runners** showing **Idle** with a label set
of `self-hosted, linux, x64` plus whatever `RUNNER_LABELS` added.
Push a commit to `main`; the `perf` job routes to the matching
host.

Multiple compose projects can share one host (e.g. separate
deployments for separate scopes): give each its own checkout,
`.env`, `-p` project name, and `RUNNER_NAME_PREFIX` so runner and
container names don't collide. The Docker image
(`zserio-ci-runner:latest`) is shared; memory caps are
per-container.

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
cd /opt/zserio-ci/ci
./refresh-runner.sh          # COMPOSE_PROJECT=<name> if not the default
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
hour (Sunday 04:00). On a host with multiple deployments, install renamed
copies of the unit pair, one per deployment, each with its own
`WorkingDirectory` and `COMPOSE_PROJECT`.

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

A self-hosted runner executes whatever code the workflow run gives
it, so the exposure is controlled at the workflow layer first and
the platform layer second:

1. **Workflow layer (committed, reviewable).** No `pull_request`-
   triggered job targets a self-hosted runner. The `perf` job — the
   only self-hosted job — carries an explicit
   `if: github.event_name == 'push' && github.repository == 'Klebert-Engineering/zserio-c'`
   guard. Changing that requires a reviewed commit to the workflow.
2. **Runner scope.** Register the runners repo-scoped (or in an
   org runner group restricted to this repo). Never place them in
   the org-default group where any repo in the org can reach them.
3. **GitHub settings.** Settings → Actions → *Fork pull request
   workflows* — require approval for all outside collaborators.
   This is the backstop for mistakes in (1); it also gates
   GitHub-hosted workflow runs on fork PRs.
4. **Containment.** The runner containers run as a non-root
   `runner` user; a compromised job is contained to the container.
   The container still shares the host kernel, so treat (1)-(3) as
   the real perimeter — containment only limits accidental damage,
   not a targeted escape.
