#!/usr/bin/env bash
#
# Runner lifecycle entrypoint.
#
# Takes a long-lived GitHub Personal Access Token (GITHUB_PAT) and
# exchanges it for a short-lived runner *registration* token at
# container start — registration tokens expire after ~1 hour, so
# they can't be baked into the image or the compose file. The PAT
# is the only secret the operator manages; it's set once in ci/.env.
#
# On SIGTERM/SIGINT (e.g. `docker compose down`) the runner is
# de-registered so it doesn't linger as an offline entry in the
# repo's runner list.
#
# Required env:
#   GITHUB_PAT    classic PAT, or fine-grained PAT, with the right
#                 scope for the registration mode chosen below:
#                   - repo mode (GITHUB_REPO set): classic `repo` scope,
#                     or fine-grained Repository → Administration:
#                     read+write on the target repo.
#                   - org mode  (GITHUB_ORG  set): classic `admin:org`
#                     scope, or fine-grained Organization → Self-hosted
#                     runners: read+write on the target org.
# Required env (exactly one of):
#   GITHUB_REPO   owner/name, e.g. Klebert-Engineering/zserio-c — the
#                 runner is bound to a single repository.
#   GITHUB_ORG    organization name, e.g. Klebert-Engineering — the
#                 runner is bound to an org and serves every repo in
#                 the org's runner group it's added to (default group
#                 = all repos). Future-proof when more repos join.
# Optional env:
#   RUNNER_NAME   defaults to the container hostname
#   RUNNER_LABELS extra comma-separated labels added to the agent's
#                 automatic set. The agent already self-reports
#                 `self-hosted`, `linux`, and the host arch
#                 (`x64`/`arm64`/`arm`), so RUNNER_LABELS carries only
#                 the application-specific ones (`perf,primary`,
#                 `perf,pi4`, ...). Hardcoding the auto labels here
#                 broke the Pi setup — the Pi ended up tagged both
#                 `x64` (forced) and `arm64` (auto-detected), matching
#                 jobs it can't actually build.

set -euo pipefail

: "${GITHUB_PAT:?GITHUB_PAT must be set (see ci/.env.example)}"

GITHUB_REPO="${GITHUB_REPO:-}"
GITHUB_ORG="${GITHUB_ORG:-}"
if [[ -n "${GITHUB_REPO}" && -n "${GITHUB_ORG}" ]]; then
    echo "entrypoint: set exactly one of GITHUB_REPO or GITHUB_ORG, not both" >&2
    exit 1
fi
if [[ -n "${GITHUB_ORG}" ]]; then
    API="https://api.github.com/orgs/${GITHUB_ORG}"
    RUNNER_URL="https://github.com/${GITHUB_ORG}"
    SCOPE_DESC="org ${GITHUB_ORG}"
elif [[ -n "${GITHUB_REPO}" ]]; then
    API="https://api.github.com/repos/${GITHUB_REPO}"
    RUNNER_URL="https://github.com/${GITHUB_REPO}"
    SCOPE_DESC="repo ${GITHUB_REPO}"
else
    echo "entrypoint: set GITHUB_REPO=owner/name (repo scope)" \
         "or GITHUB_ORG=org (org scope)" >&2
    exit 1
fi

RUNNER_NAME="${RUNNER_NAME:-$(hostname)}"
RUNNER_LABELS="${RUNNER_LABELS:-}"

RUNNER_DIR="/home/runner/actions-runner"
cd "${RUNNER_DIR}"

# --- Exchange the PAT for a registration token -----------------------------
get_registration_token() {
    curl -fsSL -X POST \
        -H "Authorization: Bearer ${GITHUB_PAT}" \
        -H "Accept: application/vnd.github+json" \
        "${API}/actions/runners/registration-token" \
        | jq -r .token
}

# --- De-register on shutdown ----------------------------------------------
deregister() {
    echo "entrypoint: de-registering runner ${RUNNER_NAME}"
    local remove_token
    remove_token="$(curl -fsSL -X POST \
        -H "Authorization: Bearer ${GITHUB_PAT}" \
        -H "Accept: application/vnd.github+json" \
        "${API}/actions/runners/remove-token" | jq -r .token || true)"
    if [[ -n "${remove_token}" && "${remove_token}" != "null" ]]; then
        ./config.sh remove --token "${remove_token}" || true
    fi
    exit 0
}
trap deregister SIGTERM SIGINT

# --- Register --------------------------------------------------------------
# If a previous run left a .runner config behind (persistent _work
# volume), reuse it; otherwise register fresh.
if [[ ! -f "${RUNNER_DIR}/.runner" ]]; then
    echo "entrypoint: registering runner ${RUNNER_NAME} for ${SCOPE_DESC}"
    reg_token="$(get_registration_token)"
    # `--labels` carries ONLY our custom labels. The agent auto-adds
    # `self-hosted`, `linux`, and the host arch, so we don't repeat
    # those (and don't lie about the arch).
    config_args=(
        --unattended
        --url "${RUNNER_URL}"
        --token "${reg_token}"
        --name "${RUNNER_NAME}"
        --work "_work"
        --replace
    )
    if [[ -n "${RUNNER_LABELS}" ]]; then
        config_args+=(--labels "${RUNNER_LABELS}")
    fi
    ./config.sh "${config_args[@]}"
else
    echo "entrypoint: reusing existing runner registration for ${RUNNER_NAME}"
fi

# --- Run -------------------------------------------------------------------
# `& wait` so the SIGTERM trap fires promptly instead of being held
# until run.sh returns on its own.
./run.sh & wait $!
