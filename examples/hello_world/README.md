# `examples/hello_world` — minimal end-to-end demo

**Phase 0 status: empty.** Phase 2 will populate this with:

- `hello.zs` — a tiny zserio schema (a struct with a few primitive fields).
- Generated `hello.c` / `hello.h` (committed for inspection; regenerated
  from the schema by the build).
- `main.c` — uses the generated code with the malloc-backed default
  allocator to encode and decode a value, asserting round-trip equality.
