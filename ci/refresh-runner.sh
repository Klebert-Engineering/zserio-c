#!/usr/bin/env bash
#
# refresh-runner.sh — keep the self-hosted runner agent current without
# the in-container self-update path.
#
# The runner is launched with --disableupdate (see entrypoint.sh): its
# self-update deletes and replaces bin/ + externals/ via a detached
# process that must outlive the listener restart. In a container that
# process dies when the entrypoint exits on the update, leaving
# bin/Runner.Listener deleted and the container crash-looping with exit
# 127 — reliably so on a slow disk, where the swap takes tens of seconds.
# So the agent version is pinned in the image (Dockerfile RUNNER_VERSION)
# and upgraded HERE instead, at build time, where a slow or flaky link
# just means a slower build, never a half-applied swap.
#
# Idempotent: checks the latest actions/runner release and only rebuilds
# + recreates when the pinned version is behind. Safe to run on a timer
# (see zserio-runner-refresh.timer). Recreating aborts a job that happens
# to be mid-flight, so schedule it for a quiet hour; jobs are infrequent
# on this pool.
#
# Optional env:
#   COMPOSE_PROJECT   passed to `docker compose -p` so the right
#                     deployment is targeted on a host that runs more than
#                     one deployment.
#                     Unset -> compose's own resolution (COMPOSE_PROJECT_NAME
#                     in .env, else the directory name).

set -euo pipefail

# Operate on the deployment this script lives in, regardless of the
# caller's working directory (systemd, cron, ...).
cd "$(dirname "$(readlink -f "$0")")"

compose() {
    if [[ -n "${COMPOSE_PROJECT:-}" ]]; then
        docker compose -p "${COMPOSE_PROJECT}" "$@"
    else
        docker compose "$@"
    fi
}

latest="$(curl -fsSL https://api.github.com/repos/actions/runner/releases/latest \
    | jq -r .tag_name | sed 's/^v//')"
if [[ -z "${latest}" || "${latest}" == "null" ]]; then
    echo "refresh-runner: could not determine the latest runner version" >&2
    exit 1
fi

current="$(grep -oP '^ARG RUNNER_VERSION=\K.*' Dockerfile)"
if [[ -z "${current}" ]]; then
    echo "refresh-runner: no ARG RUNNER_VERSION line in Dockerfile" >&2
    exit 1
fi

if [[ "${latest}" == "${current}" ]]; then
    echo "refresh-runner: runner ${current} is current, nothing to do"
    exit 0
fi

echo "refresh-runner: updating runner ${current} -> ${latest}"
sed -i "s|^ARG RUNNER_VERSION=.*|ARG RUNNER_VERSION=${latest}|" Dockerfile

# Build first: if the download fails (a flaky link), the running
# containers are left untouched. Only recreate once the new image exists.
compose build
compose up -d --force-recreate

echo "refresh-runner: runner updated to ${latest}"
