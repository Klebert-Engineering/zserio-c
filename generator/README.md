# `generator/` — legacy Java extension

A Java extension that plugs into upstream `ndsev/zserio` (vendored
under `third_party/zserio/`) and emits C99 against the runtime.

This is **legacy**. The supported generator is the C-native
`zserio_c_gen` (CLI under `parser/cli/main.c`, build target
`zserio_c_gen` from the top-level CMake). The Java extension here
predates the C-native parser; it's retained because the
xvalidation harness needs a way to invoke the *upstream cpp*
generator to produce the reference output we compare against.

## What still uses this

- `tests/xvalidation/` — invokes both the upstream cpp generator
  (for the reference output) and our C-native generator (for the
  output under test). The cpp side goes through the upstream
  toolchain, which this extension was built to integrate with.

## What does not

- Anything you'd build with zserio-c. The CLI you want is
  `zserio_c_gen` — see [`docs/cli.md`](../docs/cli.md).

## Building

The Java jars are built on demand by `tools/build_*_jar` scripts;
they're not part of the default CMake target. The top-level CMake
adds them only when the xvalidation target is requested.

If you don't run xvalidation, you don't need this directory.
