# Integration guide — `zserio-c`

This document is the integrator-facing companion to the safety
case. It collects the responsibilities the safety case delegates
to whoever links `zserio-c` into a wider system — primarily an
automotive ECU software stack, but the items are mode-agnostic.

The canonical statement of each delegation lives in
`docs/safety-case/hazard-analysis.md` (rightmost column) and in
`docs/safety-case/item-definition.md` § Assumed system context.
This doc is a quick checklist that points back to those for the
underlying rationale.

## What `zserio-c` does for you

To set expectations: the runtime is responsible for, and the CI
gates enforce:

- **Wire-format compatibility** with upstream `ndsev/zserio
  v2.18.1` for every encode/decode pair (REQ-FN-001, `xvalidation`
  gate over 183 schemas × 5 seeds per commit).
- **Bounded, deterministic execution.** Recursion-free runtime
  (MISRA Rule 17.2) with depth-gated generated readers — decode
  fails with `ZSERIO_ERR_MAX_DEPTH_EXCEEDED` past the tunable
  ceiling (REQ-FS-003) — bounded loops (REQ-FS-005), bounded
  per-function stack frames and bounded transitive call-chain
  stack (`stack` gate, see `docs/safety-case/stack-budget.md`).
- **Explicit error reporting.** Every public function returns a
  `zserio_status_t` or void — no exceptions, no `errno`, no
  hidden state — see REQ-FS-002.
- **No direct heap calls.** All allocation routes through the
  caller-supplied vtable (REQ-FS-001).
- **No runtime reflection** in the production build (REQ-FS-008).
- **Static MISRA-C:2012** compliance on both runtime sources and
  the generator's output (`misra` and `misra-generator` CI jobs).

Everything else listed below is yours to wire up.

## Integration responsibilities

### 1. Supply an allocator (H-03, REQ-FS-001)

The runtime requires a `zserio_allocator_t` vtable on every
allocating call. Three options, in increasing order of automotive
fitness:

- **`zserio_allocator_libc()`** — the reference libc wrapper in
  `runtime/src/allocator.c`. Suitable for development and for
  production where libc is qualified.
- **`zserio_allocator_arena(&arena)`** — the reference bump arena
  shipped in the runtime. The caller declares a `zserio_arena_t`
  over a fixed-size backing buffer; `alloc` is a pointer add,
  `free` is a no-op, `zserio_arena_reset(&arena)` reclaims the
  whole arena in O(1) between decodes. The arena gives you the
  bounded-memory property the safety case cannot guarantee on its
  own AND the performance envelope the libc allocator masks (the
  per-commit perf gate shows the arena workload running 1.2–2.0×
  faster than upstream cpp's `std::pmr` equivalent on the same
  payload). Typical use:
  ```c
  static uint8_t pool[64 * 1024];          /* fixed budget per channel */
  zserio_arena_t arena;
  zserio_arena_init(&arena, pool, sizeof pool);
  const zserio_allocator_t alloc = zserio_allocator_arena(&arena);
  /* decode using `alloc`; the arena's high_water field tells you
   * how much of `pool` the worst case consumed. */
  zserio_arena_reset(&arena);              /* between messages */
  ```
  Size `pool` from `arena.high_water` measured on representative
  worst-case inputs. The arena returns `NULL` on exhaustion, which
  propagates as `ZSERIO_ERR_OUT_OF_MEMORY` through every generated
  reader — no abort.
- **A fixed-pool / `pmr`-style implementation of your own.** The
  vtable is three function pointers (`alloc`, `free`, `realloc`);
  see `runtime/include/zserio/allocator.h` for the contract. Use
  this when neither libc nor a single monotonic arena fit the
  channel (e.g. multi-message reuse with selective free).

The integrator's choice shapes the memory-bound analysis. Whichever
allocator you supply, every `alloc` call site in the runtime
already checks the return value and propagates a status code on
failure — your allocator must return `NULL` on exhaustion rather
than abort.

### 2. Size the receive buffer (H-01, REQ-FS-006)

The runtime validates every read against the buffer-bit-size you
hand `zserio_bitstream_reader_init`. **You** decide that size from
the channel's maximum message envelope. The runtime guarantees it
will not read past the buffer; it cannot tell you the message is
"complete" beyond what the schema says.

### 3. Handle the status codes (H-01, H-02, H-03)

Every public function returns `zserio_status_t`. The expected
recovery is application-level:

- `ZSERIO_OK` — continue.
- `ZSERIO_ERR_END_OF_STREAM` / `_INVALID_TAG` / `_INVALID_ENCODING`
  — the message is malformed; discard and (depending on channel
  policy) drop, log, or request retransmission.
- `ZSERIO_ERR_OUT_OF_MEMORY` — your allocator returned NULL; back
  off, retry later, or fail the message.
- `ZSERIO_ERR_VALUE_OUT_OF_RANGE` / `_INVALID_ARGUMENT` — your code
  passed an out-of-bounds value to an encoder; this is a
  programming error.

Full list and exact semantics: `runtime/include/zserio/status.h`.

### 4. Impose a wall-clock timeout if needed (H-02)

Decode time is bounded by `(buffer_bit_size, schema_complexity)` —
the runtime does not loop unboundedly. If your channel has a hard
real-time deadline shorter than the implied bound, supervise the
decode call from a timer or watchdog. The runtime exposes no
internal cooperative cancellation.

### 5. Wrap in a CRC / MAC if integrity matters (H-04)

The zserio wire format has no checksum or message authentication
code. Bytes flipped in transit will decode silently to a different
(but possibly well-formed) value, or to a malformed-stream error
that does not name the cause. If end-to-end protection is required
— and it is for any safety-relevant channel — wrap the byte
stream in a CRC or MAC layer before feeding it to the decoder.
This is delegated by the safety case at the item-definition level.

### 6. Pin the schema version per channel (H-05)

Zserio has no in-band schema version field. Encoder and decoder
that disagree on the schema will produce results that decode
"successfully" but mean something different. Pin one canonical
schema version per channel at integration time and treat schema
deployment as a coordinated rollout.

If you need an in-band guard, the generator can emit a schema
hash table on request (`-withTypeInfoCode`); compare it across
endpoints out-of-band.

### 7. Size the target stack (H-09)

Two ceilings the runtime enforces in CI cover its own stack
contribution:

- per-function frame ≤ 256 B (current worst: 160 B in
  `zserio_read_extern`);
- transitive worst-case chain ≤ 1024 B (current worst: 512 B
  over a 7-frame chain rooted at `zserio_read_extern`).

The transitive number is built without LTO, so production builds
that enable `-flto` see a smaller real worst case. Size your
target's thread stack budget against the **1024 B** ceiling plus:

- your own application frames above the `zserio_*` entry point,
- the generator's per-`_read` frames for your specific schema
  (re-run `tools/measure_stack_usage.py` against the generated
  output to measure — the same tool works on any cmake target),
- the libc / RTOS allocator's frame cost on each `alloc` / `free`.

A 4 KiB or 8 KiB thread stack is comfortable for typical
automotive payloads.

### 8. Qualify your toolchain (H-10)

The tool-qualification records under `docs/safety-case/` describe
*our* validation of gcc, cppcheck, lcov/gcov, and the
`zserio_c.jar` generator (see `safety-case.md`). They are
inputs to your own production toolchain qualification — replay
the validation criteria against the compiler / static analyzer /
coverage tool / generator version you actually ship.

For the generator: rebuilding `zserio_c.jar` from a pinned commit
of this repository is deterministic given the same JDK 17 + Ant;
the jar is not cached in CI (see `tool-qualification-generator.md`).

## Out of scope

The following are intentionally not the runtime's responsibility.
Pointers if you need to address them:

| Concern                                  | Where to look                                                                  |
| ---------------------------------------- | ------------------------------------------------------------------------------ |
| Side-channel resistance                  | Outside the safety case; the runtime makes no constant-time guarantees.        |
| Multi-threaded use of one reader/writer  | Not supported. Use one `bitstream_reader_t` / `_writer_t` per thread (REQ-FS-007). |
| Streaming / chunked decode               | Out of scope; the runtime decodes against a fully-buffered message.            |
| Schema evolution / forward compatibility | Out of scope; pin one schema version per channel (H-05).                       |
| RPC / pub-sub transport                  | Generated `service` / `pubsub` glue is transport-agnostic; you bind a transport via the `zserio_service_t` / `zserio_pubsub_t` vtables (`zserio/service.h`, `zserio/pubsub.h`; see `docs/design/service-pubsub.md`). |

## Cross-references

- `docs/safety-case/hazard-analysis.md` — full HARA table.
- `docs/safety-case/item-definition.md` § Assumed system context.
- `docs/safety-case/safety-case-report.md` § Open items category 3
  (Residual risks delegated to the integrator).
- `docs/safety-case/stack-budget.md` — the worst-case stack
  derivation behind item 7.
- `docs/safety-case/tool-qualification-*.md` — the TQ records
  behind item 8.
