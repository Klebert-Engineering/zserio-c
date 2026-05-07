# `generator/` — zserio-c extension for the upstream zserio toolchain

A Java extension that plugs into upstream `ndsev/zserio` (vendored under
`third_party/zserio/`) and emits C99 against `runtime/`.

**Phase 0 status: empty.** Phase 2 will:

- Add `build.gradle.kts` (or `pom.xml`) targeting the zserio Extension SPI
  exposed by the pinned upstream version `v2.18.1`.
- Place Java sources under `src/main/java/`.
- Place FreeMarker code templates under `templates/`.

The generator is on the safety path because it produces code that is
itself part of the safety-relevant artifact. Phase 5 work products
include tool qualification for it.
