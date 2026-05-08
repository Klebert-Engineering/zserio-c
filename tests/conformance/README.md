# `tests/conformance/`

Phase 4 cross-validation suite: encode and decode through both the
zserio-c runtime and the upstream `ndsev/zserio` C++ runtime, byte-
compare. The strategy and harness layout are documented in
`docs/upstream-analysis/06-test-corpus.md`.

## Files

- **`error_mapping.csv`** — maps each upstream zserio C++ throw site
  (file:line) to the corresponding `zserio_status_t` value our runtime
  returns for the same error. Used by the negative-test side of the
  conformance harness to assert that "upstream throws X here ↔ we
  return Y here". Maintained alongside the `zserio_status_t` enum;
  any new status code or any new throw site mapping goes through this
  file.

## Phase 4 prerequisites (recorded for visibility)

Before this directory becomes useful:

1. The upstream test data must be pulled. From the repo root:
   ```
   git -C third_party/zserio submodule update --init test/data
   ```
   (or `git submodule update --init --recursive` from the very top.)
2. A Java toolchain is needed to build the upstream `zserio.jar` and
   our generator JAR.
3. The runtime (Phase 1) and the generator (Phase 2) must exist.
   Until then, the harness sources are not present.
