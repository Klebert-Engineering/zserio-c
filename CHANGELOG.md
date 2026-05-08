# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- Initial repository scaffold (Phase 0): docs, requirements, CMake, CI skeleton.
- Upstream `ndsev/zserio` vendored as submodule at `third_party/zserio`,
  pinned to `v2.18.1`.
- Starter requirement set: REQ-FS-001..008 (safety), REQ-FN-001 (wire
  compat), REQ-PF-001..002 (performance).
- Six upstream-analysis deep-dives at `docs/upstream-analysis/00..06`
  covering structural survey, schema language scope, wire format,
  Java extension SPI, C++ runtime internals (with `pmr/` deep-read),
  ASIL pain-point enumeration, and test corpus reuse plan.
- Public runtime API contract locked: `runtime/include/zserio/allocator.h`
  (allocator vtable + `zserio_allocator_libc()`) and
  `runtime/include/zserio/status.h` (`zserio_status_t` enum +
  `ZSERIO_TRY` macro).
- `tests/conformance/error_mapping.csv` — maps the 21 v1-relevant
  upstream throw sites to specific `ZSERIO_ERR_*` codes; consumed by
  the Phase 4 conformance harness's negative-test side.
