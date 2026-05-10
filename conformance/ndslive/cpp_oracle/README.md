# cpp wire-format oracle

This directory holds the pieces needed to build upstream's C++ NDS Live
reader as a **wire-format oracle**: an authoritative reference that we
can diff our own zserio-c reader against, bit by bit, to localise
divergence to a specific field instead of guessing from an end-state
error code.

## Files

- `read_tile_cpp.cpp` — oracle driver. Decodes `tile.bin` via upstream's
  `SmartLayerTile` + dispatches a chosen layer to the matching
  layer-specific reader. The cpp side is the *reference*; if upstream
  decodes a byte sequence one way and we decode it another, upstream
  wins.
- `bitstream_reader_trace.patch` — small patch against the extracted
  `cpp/zserio_runtime/zserio/BitStreamReader.cpp` that adds a single
  trace function `zsc_trace_set_enabled(bool)` and one `zsc_trace()`
  call inside `readBitsImpl` (the choke-point every primitive read goes
  through). The patch is idempotent — `run_oracle.sh` guards against
  double-application.
- `run_oracle.sh` — script that applies the patch, builds the cpp
  reference + oracle driver, and runs it on `tile.bin`. Optionally
  scopes the trace to a single layer with `--layer N`.

## Trace format

Both this oracle and zserio-c's own runtime emit one line per
primitive read:

```
<bit_pos_after> read <num_bits> <value_as_uint64>
```

These two traces are intended to be `diff`'d directly. The first
differing line tells you the exact bit position where the two readers
disagree and what each consumed. From there it's a one-grep walk back
to the schema field and the generator path that emits its read.

## Workflow

1. Re-stage the cpp tree (overwrites any patches in `cpp/`):
   ```
   bash conformance/ndslive/extract.sh
   ```
2. Build the oracle + decode layer N into `/tmp/cpp.trace`:
   ```
   bash conformance/ndslive/cpp_oracle/run_oracle.sh --layer 3 /tmp/cpp.trace
   ```
3. Build our reader + decode the same layer into `/tmp/c.trace`:
   ```
   bash conformance/ndslive/run.sh   # builds the C reader
   ZSERIO_TRACE=/tmp/c.trace ZSERIO_TRACE_LAYER=3 \
       build-conformance/read_tile conformance/ndslive/tile.bin
   ```
4. Diff:
   ```
   diff /tmp/cpp.trace /tmp/c.trace | head
   ```

## Why this oracle exists

zserio-c is a from-scratch C99 port of upstream's runtime + generator
(see `requirements/` for the ASIL-B / MISRA-C constraints driving the
rewrite). The wire format must match upstream **byte for byte**.
Differential testing against tile.bin alone gives only an end-state
error code; without an oracle we'd have to read upstream's FreeMarker
templates and replay their semantics in our head. The oracle skips
that — when readers diverge, the diff localises the field within
seconds. The trace hook adds one branch + one fprintf per primitive
read; production builds compile both files without any tracing
overhead (the `ZSERIO_TRACE` env var has to be set AND
`zsc_trace_set_enabled(true)` has to have been called).
