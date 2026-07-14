# `tests/xvalidation/` — schema-driven cross-validation harness

Verifies wire-format interoperability (REQ-FN-001): for each
schema in `schemas.txt`, the C generator + C runtime must produce
**byte-identical** wire output to upstream's C++ generator + C++
runtime for the same struct value.

The pattern is intentionally narrow:

1. Generate code on both sides into per-schema work dirs:
   - C99 via `generator/build/jar/zserio_c.jar` (our generator)
   - C++ via `build-jars/zserio.jar` (upstream's full tool with cpp
     extension included)
2. Compile each side against its runtime (`runtime/` for ours,
   `third_party/zserio/compiler/extensions/cpp/runtime/src` for cpp).
3. Per seed:
   - `c_driver <seed> bytes_c.bin` — deterministic fill from `seed`,
     encode, write bytes.
   - `cpp_driver bytes_c.bin bytes_cpp.bin` — decode the C output,
     re-encode via cpp, write bytes.
   - `cmp bytes_c.bin bytes_cpp.bin` — byte-equal means upstream
     read our output AND re-emitted the same bytes ⇒ the wire format
     agrees for this struct value.

Mismatch ⇒ enable the per-primitive-read trace hook
(`ZSERIO_TRACE`, see `runtime/include/zserio/bitstream.h`) on
both sides and diff the traces against the failing schema to
localize the divergent field.

## Adding a schema

1. Pick a `.zs` file under `third_party/zserio-test-data/language/`,
   or author one under `schemas/` (in-tree synthetic schemas — used
   for shapes the upstream corpus doesn't cover, e.g. the
   production-shape `synthetic/layered_container.zs`). The harness
   re-derives the staging dir from the schema's `package` line, so
   any path under either root works.
2. Add the path to `schemas.txt`.
3. Create `harnesses/<schema_stem>/c_driver.c` and `cpp_driver.cpp`.
   Both drivers are small (~30 lines each) and follow the contract
   in the existing examples. Regenerate the bidirectional driver
   with `python3 tests/xvalidation/gen_roundtrip.py`.
4. Run `bash tests/xvalidation/run_xvalidation.sh`.
5. Bump the schema count in the docs (`grep -rn` for the old count).

## Files

- `run_xvalidation.sh` — orchestrator. Generates code on both sides,
  builds drivers, runs each seed, diff'd results.
- `schemas.txt` — list of schemas to validate, one path per line
  (resolved against `third_party/zserio-test-data/language/`, then
  `schemas/`).
- `schemas/` — in-tree synthetic schemas.
- `harnesses/<schema_stem>/` — per-schema drivers.
- `<repo>/build-xvalidation/` — work tree (gitignored). Per-schema
  subdirs hold the generated code on both sides, the compiled
  drivers, and the bytes_c/bytes_cpp files from each seed (kept for
  diagnostic use on failure).

## Conventions

- C driver: fills a struct with deterministic data derived from `seed`
  (a tiny LCG, no external PRNG dependency), encodes, writes bytes.
- Cpp driver: reads bytes, decodes via upstream, re-encodes via
  upstream, writes bytes.
- Both sides must agree on the buffer-size unit:
  `zserio_bitstream_writer_init` takes **bits** (not bytes); the
  cpp side's `BitStreamWriter` ctor takes **bytes**.
- The default seed list (`SEEDS="1 2 7 42 999"`) hits both branches
  of every optional/conditional in the current schemas. Override
  with `SEEDS="..." bash run_xvalidation.sh`.

## Why not random-generate the drivers?

We could, and probably will once the schema-shape catalog stabilises.
The hand-written drivers force us to think about which field shapes
each schema actually exercises, and they're small enough that the
maintenance cost is negligible. When a new schema-language feature
ships, one new pair of drivers per representative schema is all it
takes.

## Status

```
schemas: 8 passing × 5 seeds each = 40 round-trips, byte-equal
```

Coverage so far: fixed/auto/packed-fixed/packed-variable/packed-auto
uint8 arrays; packed struct array with mixed packed/varuint fields;
simple bitfield structure; auto-optional int.

Pending field shapes worth adding (each catches a different class of
wire-format bug if it diverges):
- choice with multiple cases
- union (no upstream wire-format test yet)
- bitmask
- enum
- constraint
- nested compound subfield
- parameterized type
- subtype
- string / extern / bytes
