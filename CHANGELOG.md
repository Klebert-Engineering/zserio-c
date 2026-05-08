# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Phase 1 complete

`.phase` advanced from 0 to 1. The runtime is feature-complete for
the in-scope schema language with **9 of 11 requirements verified**
(the two pending are `REQ-FS-008` — needs the Phase 2 generator —
and `REQ-PF-001` — Phase 6 benchmarks).

### Added (Phase 0 + Phase 1)

- Initial repository scaffold: docs, requirements, CMake, CI skeleton.
- Upstream `ndsev/zserio` vendored as submodule at `third_party/zserio`,
  pinned to `v2.18.1`.
- Starter requirement set: REQ-FS-001..008 (safety), REQ-FN-001 (wire
  compat), REQ-PF-001..002 (performance).
- Six upstream-analysis deep-dives at `docs/upstream-analysis/00..06`
  covering structural survey, schema language scope, wire format,
  Java extension SPI, C++ runtime internals (with `pmr/` deep-read),
  ASIL pain-point enumeration, and test corpus reuse plan.
- **Runtime library** (`runtime/`): allocator, status, bitstream,
  primitives, varint, float, string, extern, bytes, delta, plus
  `zserio.h` umbrella. **187 Unity tests, all green.**
- `tests/conformance/error_mapping.csv` — maps the 21 v1-relevant
  upstream throw sites to specific `ZSERIO_ERR_*` codes; consumed by
  the Phase 4 conformance harness's negative-test side.
- **MISRA C:2012 compliance**: `cppcheck --addon=misra` runs in CI
  in `--strict` mode against the runtime; five documented deviations
  under `docs/misra-deviations/D001..D005.md` cover the categorical
  acceptances (multiple returns, allocator wrapper, IEEE 754 memcpy,
  void-pointer cast, buffer pointer arithmetic).
- **Coverage gate**: lcov, currently passing at 98.43% line / 81.05%
  branch / 100% functions on the runtime.
- **Size gate (REQ-PF-002)**: per-section size measurement, baseline
  at `docs/benchmarks/sizes.baseline.json`, fails CI on >5% growth
  on any of `.text` / `.rodata` / `.data` / `.bss`. Current baseline:
  `.text` 8221 B, `.rodata` 432 B, `.data` 0 B, `.bss` 0 B.
