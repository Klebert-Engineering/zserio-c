# Self-hosted GitHub Actions runner image for zserio-c CI.
#
# Debian Bookworm base — matches the gcc 12 / glibc the project is
# developed against, so locally-reproduced builds line up with what
# this runner produces. The image carries the full toolchain every
# CI job needs, so the workflow's `apt-get install` steps become
# fast no-ops (they still run, keeping the workflow portable back to
# GitHub-hosted runners if the minutes quota resets).
#
# Build:  docker build -t zserio-ci-runner ci/
# Run:    see ci/docker-compose.yml

FROM debian:bookworm-slim

# --- Toolchain -------------------------------------------------------------
# Pinned to the package set the 11 CI jobs need:
#   build-essential, clang  -> build-and-test (gcc + clang)
#   cmake, ninja-build      -> every build job
#   binutils                -> size gate (`size`), perf LTO (`gcc-ar`)
#   cppcheck                -> MISRA gate
#   lcov                    -> coverage gate (pulls in gcov via gcc)
#   python3                 -> size/coverage/traceability/stack checkers
#   default-jdk-headless,
#   ant                     -> generator + xvalidation + perf bootstrap
#   git, curl, jq, ca-certs -> checkout, submodules, runner registration
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        clang \
        cmake \
        ninja-build \
        binutils \
        cppcheck \
        lcov \
        python3 \
        ant \
        default-jdk-headless \
        git \
        curl \
        jq \
        ca-certificates \
        sudo \
        tar \
        gzip \
    && rm -rf /var/lib/apt/lists/*

# --- Runner user -----------------------------------------------------------
# The Actions runner refuses to run as root. Give the `runner` user
# passwordless sudo so the workflow's `sudo apt-get` steps succeed
# (they're no-ops against the pre-installed toolchain, but the
# workflow still invokes them).
RUN useradd -m -s /bin/bash runner \
    && echo "runner ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/runner \
    && chmod 0440 /etc/sudoers.d/runner

# --- GitHub Actions runner -------------------------------------------------
# RUNNER_VERSION is overridable at build time; bump it when GitHub
# deprecates older runner releases.
ARG RUNNER_VERSION=2.323.0
ARG TARGETARCH=x64
RUN mkdir -p /home/runner/actions-runner \
    && cd /home/runner/actions-runner \
    && curl -fsSL -o runner.tar.gz \
        "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-${TARGETARCH}-${RUNNER_VERSION}.tar.gz" \
    && tar xzf runner.tar.gz \
    && rm runner.tar.gz \
    && ./bin/installdependencies.sh \
    && chown -R runner:runner /home/runner

COPY entrypoint.sh /home/runner/entrypoint.sh
RUN chmod +x /home/runner/entrypoint.sh && chown runner:runner /home/runner/entrypoint.sh

USER runner
WORKDIR /home/runner/actions-runner

ENTRYPOINT ["/home/runner/entrypoint.sh"]
