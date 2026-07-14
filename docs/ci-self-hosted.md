# Self-hosted CI runners

CI runs on the project's self-hosted org runners for every
**same-repo** run — pushes, and pull requests whose head branch
lives in this repository (only people with push access can create
those). The dedicated hardware is substantially faster than
GitHub-hosted runners for the heavy gates (xvalidation, fuzz, the
jar builds). The `perf` job additionally needs dedicated,
always-on, otherwise-idle measurement hosts so that benchmark
numbers are reproducible; virtualized GitHub-hosted runners have
noisy neighbors and varying hardware generations.

**Security policy (enforced in the workflows, not just settings):**
fork PRs must never reach the self-hosted machines — a fork PR
executes arbitrary code, including edited workflow files. Every
job in `.github/workflows/ci.yml` computes its runner dynamically:

```yaml
runs-on: ${{ fromJSON(github.event_name == 'pull_request'
  && github.event.pull_request.head.repo.full_name != github.repository
  && '"ubuntu-latest"' || '["self-hosted", "linux", "x64"]') }}
```

so fork PRs fall back to GitHub-hosted `ubuntu-latest` (free for
public repos — external contributors still get full CI feedback),
while same-repo runs use the org runners. The `perf` job has no
hosted fallback and simply skips for fork PRs. See
[Security](#security) for the defense-in-depth settings that back
this up.

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

The image bakes in python3 and JDK 17 (with `JAVA_HOME` set). The
jar-building jobs still carry `actions/setup-java` so the
GitHub-hosted fallback path works for fork PRs; on the image it
just resolves from the tool cache. The `apt-get install` steps
are kept as fast no-ops.

## Hardware

General CI runners just need capacity; perf hosts are additionally
the published measurement environments — their specs are part of
the benchmark methodology (`docs/benchmarks/methodology.md`), not
an implementation detail.

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
- **Disk** — the xvalidation and benchmark harnesses generate
  large trees (`build-xvalidation/`, `build-benchmarks/`). Budget
  ~20 GB of working space.
- **Idle** — a perf host must not run other workloads while a
  measurement is in flight. The workflow serializes perf runs per
  host via a concurrency group, but it cannot see load from
  outside CI.

## Scope: repo runner vs org runner

A self-hosted runner is bound to exactly one target at registration
time. `entrypoint.sh` supports two scopes; pick one per deployment:

| Scope | When to use | `ci/.env` |
|---|---|---|
| **Repo** | Binds the runner to a single repository; the runner cannot be reached by any other repo. | `GITHUB_REPO=owner/name` |
| **Org**  | One runner pool serves every repo the runner group is enabled for. The canonical `Klebert-Engineering` deployment uses this scope. | `GITHUB_ORG=org-name` |

Set **exactly one** of `GITHUB_REPO` or `GITHUB_ORG` per `.env` —
the entrypoint refuses both-set and neither-set configurations.
For the org-scoped deployment, put the runners in a runner group
**restricted to `zserio-c`** (Org → Settings → Actions → Runner
groups) — never the org-default group that serves all
repositories. Once the repo is public, the group additionally
needs **"Allow public repositories"** enabled — GitHub disables it
by default, and without it the org runners silently stop picking
up this repo's jobs (they queue forever).

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

## Deploying a runner

The canonical path is `docker compose` from a checkout of the repo
on the host. No GUI required.

```bash
git clone https://github.com/Klebert-Engineering/zserio-c.git /opt/zserio-ci
cd /opt/zserio-ci/ci

cp .env.example .env
$EDITOR .env       # GITHUB_PAT=<your PAT>
                   # GITHUB_ORG=Klebert-Engineering (the default; put
                   # the runners in a group restricted to zserio-c)
                   #
                   # On a plain CI box, leave RUNNER_LABELS unset.
                   # On the primary perf box:
                   #   RUNNER_LABELS=perf,primary
                   # Other perf hosts use perf,<their-label>
                   # (pi4 / macmini / jetson, ...).

docker compose up -d --build   # ~5 min first time (toolchain + runner agent)
docker compose logs -f          # tail to watch registration
```

Within ~30 s of containers starting, the runners appear under
**Org → Settings → Actions → Runners** showing **Idle** with a
label set of `self-hosted, linux, x64` plus whatever
`RUNNER_LABELS` added. Push a commit or open a same-repo PR; the
jobs route to the org runners, and the `perf` job to the matching
perf host.

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

1. **Workflow layer (committed, reviewable).** Every job's
   `runs-on` expression routes fork PRs to GitHub-hosted runners;
   only same-repo runs (push / maintainer PR branches) resolve to
   the self-hosted labels. The `perf` job carries an explicit
   same-repo `if:` guard and skips for fork PRs. Changing either
   requires a reviewed commit to the workflow. Note that fork PRs
   *can* edit workflow files in their own PR — which is exactly
   why the fork check keys on where the head branch lives, not on
   file content, and why layer 3 exists.
2. **Runner scope.** Keep the runners in an org runner group
   restricted to this repo (with "Allow public repositories"
   enabled once the repo is public). Never place them in the
   org-default group where any repo in the org can reach them.
3. **GitHub settings.** Settings → Actions → *Fork pull request
   workflows* — require approval for all outside collaborators.
   This is the backstop for mistakes in (1); it also gates
   GitHub-hosted workflow runs on fork PRs.
4. **Containment.** The runner containers run as a non-root
   `runner` user; a compromised job is contained to the container.
   The container still shares the host kernel, so treat (1)-(3) as
   the real perimeter — containment only limits accidental damage,
   not a targeted escape.
