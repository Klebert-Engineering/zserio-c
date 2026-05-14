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
#   GITHUB_PAT    classic PAT with `repo` scope (or fine-grained PAT
#                 with Administration: read+write on the repo)
#   GITHUB_REPO   owner/name, e.g. ke-fritz/zserio-c-shadow
# Optional env:
#   RUNNER_NAME   defaults to the container hostname
#   RUNNER_LABELS extra comma-separated labels (the default set is
#                 always self-hosted,linux,x64)

set -euo pipefail

: "${GITHUB_PAT:?GITHUB_PAT must be set (see ci/.env.example)}"
: "${GITHUB_REPO:?GITHUB_REPO must be set, e.g. ke-fritz/zserio-c-shadow}"
RUNNER_NAME="${RUNNER_NAME:-$(hostname)}"
RUNNER_LABELS="${RUNNER_LABELS:-}"

API="https://api.github.com/repos/${GITHUB_REPO}"
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
    echo "entrypoint: registering runner ${RUNNER_NAME} for ${GITHUB_REPO}"
    reg_token="$(get_registration_token)"
    ./config.sh \
        --unattended \
        --url "https://github.com/${GITHUB_REPO}" \
        --token "${reg_token}" \
        --name "${RUNNER_NAME}" \
        --labels "self-hosted,linux,x64${RUNNER_LABELS:+,${RUNNER_LABELS}}" \
        --work "_work" \
        --replace
else
    echo "entrypoint: reusing existing runner registration for ${RUNNER_NAME}"
fi

# --- Run -------------------------------------------------------------------
# `& wait` so the SIGTERM trap fires promptly instead of being held
# until run.sh returns on its own.
./run.sh & wait $!
