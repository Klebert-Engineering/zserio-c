# `examples/hello_world` — the Getting-started schema

`hello.zs` is the schema used by the
[Getting started](../../docs/getting-started.md) walkthrough — a
single struct with a primitive field and a string, enough to see
the full schema → generated C → encode → decode loop.

There is intentionally no committed code here: the walkthrough has
you run the generator and write the `main.c` yourself, end to end,
in about 10 minutes. Follow it from step 1.

For a complete, buildable example with committed generated code —
including the arena-allocator deployment pattern recommended for
embedded targets — see
[`examples/embedded_decode/`](../embedded_decode/).
