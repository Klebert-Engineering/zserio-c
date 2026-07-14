# Contributing to zserio-c

Contributions are welcome — bug reports with minimal reproducing
schemas, new cross-validation harnesses, schema-language coverage,
runtime and generator fixes, documentation. The full guide (building,
repository layout, the five test gates, code style, the
feature-addition loop, safety-case obligations) lives in
[docs/contributing.md](docs/contributing.md); this file is only the
front door.

zserio-c is [MIT-licensed](LICENSE) and uses neither a CLA nor a DCO:
by submitting a contribution you agree that it is provided under the
repository's MIT license.

The canonical dev loop:

```sh
cmake --preset gcc-debug                   # also: clang-debug, gcc-release
cmake --build --preset gcc-debug
ctest --preset gcc-debug                   # unit tests, ~30 s
./tests/xvalidation/run_xvalidation.sh     # cpp <-> C wire-format gate
```
