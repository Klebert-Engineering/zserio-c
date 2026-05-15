# Self-hosted GitHub Actions runner image for zserio-c CI.
#
# Ubuntu 24.04 (Noble) base — deliberately the same OS GitHub-hosted
# `ubuntu-latest` runs. Every committed gate baseline (size, perf,
# coverage thresholds) and every MISRA suppression was established
# on Noble's toolchain (gcc 13, cppcheck 2.13, lcov 2.x); matching
# the base image keeps those references valid and avoids a whole
# class of version-skew failures.
#
# The image carries the full toolchain every CI job needs, so the
# workflow's `apt-get install` steps are fast no-ops, and the
# `actions/setup-python` / `actions/setup-java` steps are removed
# entirely — python3 and JDK 17 are baked in here.
#
# Build:  docker build -t zserio-ci-runner ci/
# Run:    see ci/docker-compose.yml

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# --- Toolchain -------------------------------------------------------------
# Pinned to the package set the CI jobs need:
#   build-essential, clang        -> build-and-test (gcc + clang)
#   cmake, ninja-build            -> every build job
#   binutils                      -> size gate (`size`), perf LTO (`gcc-ar`)
#   cppcheck                      -> MISRA gate
#   lcov                          -> coverage gate (gcov ships with gcc)
#   python3                       -> size/coverage/traceability/stack/MISRA-addon
#   openjdk-17-jdk-headless, ant  -> generator + xvalidation + perf bootstrap
#                                    (explicit JDK 17 — Noble's default-jdk is 21)
#   git, curl, jq, ca-certs       -> checkout, submodules, runner registration
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
        ant-optional \
        openjdk-17-jdk-headless \
        git \
        curl \
        jq \
        ca-certificates \
        sudo \
        tar \
        gzip \
    && rm -rf /var/lib/apt/lists/*

# --- GitHub CLI ------------------------------------------------------------
# The workflow's "Post … as commit comment" steps shell out to `gh`.
# It's not in Ubuntu's default repos; add the official GitHub CLI apt
# source. (curl + ca-certificates from the layer above are needed here.)
RUN mkdir -p -m 755 /etc/apt/keyrings \
    && curl -fsSL https://cli.github.com/packages/githubcli-archive-keyring.gpg \
        -o /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg \
    && echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
        > /etc/apt/sources.list.d/github-cli.list \
    && apt-get update \
    && apt-get install -y --no-install-recommends gh \
    && rm -rf /var/lib/apt/lists/*

# --- Java environment ------------------------------------------------------
# `ant` and the zserio build scripts find the JDK via JAVA_HOME. The
# Ubuntu openjdk package installs to /usr/lib/jvm/java-17-openjdk-<arch>
# (`amd64` on x86, `arm64` on aarch64). Symlink to a stable path so
# JAVA_HOME stays portable across architectures.
RUN ln -s "/usr/lib/jvm/java-17-openjdk-$(dpkg --print-architecture)" /opt/java-17
ENV JAVA_HOME=/opt/java-17
RUN test -d "${JAVA_HOME}" && java -version && javac -version

# --- Runner user -----------------------------------------------------------
# The Actions runner refuses to run as root. Give the `runner` user
# passwordless sudo so the workflow's `sudo apt-get` steps succeed
# (no-ops against the pre-installed toolchain, but still invoked).
RUN useradd -m -s /bin/bash runner \
    && echo "runner ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/runner \
    && chmod 0440 /etc/sudoers.d/runner

# --- GitHub Actions runner -------------------------------------------------
# RUNNER_VERSION is overridable at build time; bump it when GitHub
# deprecates older runner releases.
#
# Runner agent URL is arch-specific. GitHub publishes
#   linux-x64 / linux-arm64 / linux-arm
# tarballs; we map dpkg's amd64 -> x64 and pass arm64/arm through.
# A Pi 5 (aarch64) and an x86 server build from the same Dockerfile
# and get the right tarball each.
ARG RUNNER_VERSION=2.334.0
RUN mkdir -p /home/runner/actions-runner \
    && cd /home/runner/actions-runner \
    && deb_arch="$(dpkg --print-architecture)" \
    && case "${deb_arch}" in \
         amd64) runner_arch=x64 ;; \
         arm64) runner_arch=arm64 ;; \
         armhf) runner_arch=arm ;; \
         *) echo "unsupported architecture: ${deb_arch}" >&2; exit 1 ;; \
       esac \
    && curl -fsSL -o runner.tar.gz \
        "https://github.com/actions/runner/releases/download/v${RUNNER_VERSION}/actions-runner-linux-${runner_arch}-${RUNNER_VERSION}.tar.gz" \
    && tar xzf runner.tar.gz \
    && rm runner.tar.gz \
    && ./bin/installdependencies.sh \
    # _work lives in the container's writable layer (no named volume —
    # see docker-compose.yml). Pre-create it runner-owned anyway so the
    # ownership is correct regardless of how it's mounted.
    && mkdir -p /home/runner/actions-runner/_work \
    && chown -R runner:runner /home/runner

COPY entrypoint.sh /home/runner/entrypoint.sh
RUN chmod +x /home/runner/entrypoint.sh && chown runner:runner /home/runner/entrypoint.sh

USER runner
WORKDIR /home/runner/actions-runner

ENTRYPOINT ["/home/runner/entrypoint.sh"]
