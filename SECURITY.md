# Security Policy

## Supported versions

zserio-c is pre-1.0. Only the latest `0.1.x` release receives security
fixes; earlier releases are not maintained.

| Version | Supported |
|---|---|
| latest `0.1.x` | yes |
| anything older | no |

## Reporting a vulnerability

Please do not report security issues through public GitHub issues.

- **Primary:** GitHub private vulnerability reporting — open the
  repository's **Security** tab and select **"Report a vulnerability"**.
- **Fallback:** email <f.klebert@klebert-engineering.de>.

You will receive an acknowledgment within 7 days.

## Scope

The runtime (`runtime/`), the generator (`parser/`), and the code it
generates are all in scope. The decode path processes untrusted input,
so decoder robustness issues — out-of-bounds reads, crashes or hangs on
malformed streams, integer overflow in sizing — are security-relevant
and in scope. A libFuzzer harness covers the decode path
(`tests/fuzz/`, driven by `tools/run_fuzz.sh`); fuzzer-found crashes
are welcome reports.

## Coordinated disclosure

Please allow a reasonable window for a fix and a release before public
disclosure. Reporters are credited in the release notes unless they
prefer otherwise.
