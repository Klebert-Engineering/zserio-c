# Self-hosted CI runners

GitHub-hosted minutes are a finite monthly quota. This document
describes the self-hosted runner setup that takes over when the
quota is exhausted (and, longer term, is the cheaper steady state).

Everything needed lives in [`ci/`](../ci/):

| File | Purpose |
| --- | --- |
| `ci/Dockerfile` | Builder image — Debian Bookworm + the full CI toolchain + the GitHub Actions runner agent |
| `ci/entrypoint.sh` | Runner lifecycle — exchanges a PAT for a registration token at start, de-registers on stop |
| `ci/docker-compose.yml` | Three runner instances on one host, sharing the image |
| `ci/.env.example` | Template for the one secret the operator manages (a GitHub PAT) |

The CI workflow (`.github/workflows/ci.yml`) targets
`runs-on: [self-hosted, linux, x64]`. The `apt-get install` steps in
the workflow are intentionally kept — they're fast no-ops against
the pre-installed toolchain in the image, and keep the workflow
portable back to GitHub-hosted runners if the quota resets and you
want to switch back.

## Hardware

Reference target: a Synology DS3018xs (Pentium D1508, 2 cores / 4
threads, 32 GB RAM, x86-64, always-on, runs Container Manager).

The constraints that actually matter:

- **x86-64** — keeps the `size` and `perf` baselines comparable to
  the values already committed under `docs/benchmarks/`. An ARM host
  works too but needs a one-time rebaseline of those two gates.
- **RAM** — the Java/ant jar builds want ~2 GB headroom; the LTO
  link of the `ndslive_tile` perf workload can spike to ~4 GB
  transient. 8 GB is the floor, more is comfortable. 32 GB is
  generous.
- **Cores** — 2 is enough; jobs just serialise more. Three runner
  instances (the compose default) let GitHub dispatch the lighter
  jobs in parallel, roughly halving a full run's wall-clock.
- **Disk** — the xvalidation and perf jobs generate large trees
  (`build-xvalidation/`, `build-benchmarks/`, 1500+ files for the
  ndslive schema). Budget ~20 GB of working space.

## One-time: create the PAT

The runners need a token to register themselves. Create a GitHub
Personal Access Token with permission to administer repository
runners:

- **Classic PAT**: `repo` scope.
- **Fine-grained PAT**: `Administration: Read and write` on the
  `zserio-c-shadow` repository.

GitHub → Settings → Developer settings → Personal access tokens.

The PAT is exchanged for a short-lived *registration* token at
container start (`entrypoint.sh`); it is never baked into the image
or the compose file. It lives only in `ci/.env`, which is gitignored.

## Deploying to the Synology NAS

DSM 7.x ships **Container Manager** (the rebranded Docker package).
Two ways to deploy — pick whichever you're more comfortable with.

### Option A — Container Manager GUI (Projects)

1. **Get the files onto the NAS.** Either:
   - clone the repo on the NAS (`git clone ... /volume1/docker/zserio-ci`), or
   - via File Station, upload the `ci/` directory to e.g.
     `/volume1/docker/zserio-ci/`.
2. **Create `ci/.env`** next to `docker-compose.yml`:
   `cp ci/.env.example ci/.env`, then edit it and paste your PAT.
3. In **Container Manager → Project → Create**:
   - Project name: `zserio-ci`
   - Path: the folder containing `docker-compose.yml`
   - It detects the compose file; click through to build + start.
4. Container Manager builds the image (a few minutes — it's
   downloading the toolchain and the runner agent) and starts the
   three runner containers.

### Option B — SSH + docker compose CLI

1. Enable SSH on the NAS (Control Panel → Terminal & SNMP).
2. `ssh your-admin@nas-host`
3. Stage the files (clone or scp the `ci/` directory).
4. Create `ci/.env` from the example, paste the PAT.
5. Build and start:
   ```
   cd /volume1/docker/zserio-ci
   sudo docker compose -f ci/docker-compose.yml up -d --build
   ```
   (`docker compose` is the bundled v2 plugin in current DSM; older
   DSM uses `docker-compose`.)

### Verify

Within ~30 s of the containers starting, three runners named
`zserio-ci-1/2/3` should appear under the repo's
**Settings → Actions → Runners**, each showing **Idle**. Push any
commit (or open a PR) and the jobs route to them.

## Operations

- **Logs**: `docker logs -f zserio-ci-runner-1` (or the equivalent
  in Container Manager) shows registration + each job's runner-side
  output.
- **Restart**: `docker compose -f ci/docker-compose.yml restart`.
  The `_work` directory is a named volume, so a restart reuses the
  `build-jars/` cache rather than rebuilding the upstream zserio
  jars from scratch.
- **Stop / remove**: `docker compose -f ci/docker-compose.yml down`.
  `entrypoint.sh` traps the stop signal and de-registers each runner
  so it doesn't linger as an offline entry in the repo's runner
  list.
- **Bump the runner agent**: GitHub deprecates old runner releases
  periodically. Edit `RUNNER_VERSION` in `ci/Dockerfile` and
  `docker compose ... up -d --build`.
- **Disk pressure**: if the `_work` volumes grow large, stop the
  stack and `docker volume rm zserio-ci_runner-{1,2,3}-data`. The
  next start re-registers and rebuilds the jar cache once.

## Security

Self-hosted runners on a **private** repository are safe — only
collaborators can push code that runs on them.

If `zserio-c-shadow` is ever made **public**, this changes: a fork
pull request could run arbitrary code on the NAS. Before going
public, either disable self-hosted runners for fork PRs or restrict
them under **Settings → Actions → Fork pull request workflows**.
The runner containers run as a non-root `runner` user with
passwordless sudo scoped to the container — a compromised job is
contained to the container, but the container still shares the NAS
kernel, so the public-repo caveat stands.
