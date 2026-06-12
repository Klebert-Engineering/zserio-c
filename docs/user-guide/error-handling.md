# Error handling

Every fallible function in zserio-c returns `zserio_status_t`.
The runtime never aborts, never longjmps, never throws. This page
covers the status taxonomy and the propagation pattern.

## The status type

`zserio_status_t` is an enum from `<zserio/status.h>`. `ZSERIO_OK`
is zero; all error codes are non-zero, organised into five bands:

```c
typedef enum zserio_status {
    ZSERIO_OK = 0,

    /* Argument validation at the public API boundary */
    ZSERIO_ERR_INVALID_ARGUMENT,
    ZSERIO_ERR_INVALID_NUM_BITS,
    ZSERIO_ERR_INVALID_BUFFER,
    ZSERIO_ERR_INVALID_POSITION,
    ZSERIO_ERR_VALUE_OUT_OF_RANGE,

    /* Stream and buffer state */
    ZSERIO_ERR_END_OF_STREAM,
    ZSERIO_ERR_BUFFER_OVERFLOW,
    ZSERIO_ERR_BUFFER_UNDERFLOW,
    ZSERIO_ERR_BUFFER_TOO_LARGE,

    /* Encoding / decoding semantics */
    ZSERIO_ERR_INVALID_ENCODING,
    ZSERIO_ERR_INVALID_ENUM,
    ZSERIO_ERR_INVALID_TAG,
    ZSERIO_ERR_CONSTRAINT_VIOLATED,
    ZSERIO_ERR_OFFSET_MISMATCH,
    ZSERIO_ERR_ARRAY_TOO_LARGE,

    /* Resource */
    ZSERIO_ERR_OUT_OF_MEMORY,

    /* Safety limits (zserio-c specific) */
    ZSERIO_ERR_MAX_DEPTH_EXCEEDED
} zserio_status_t;
```

`ZSERIO_OK == 0` lets `if (st) handle_error();` work, but the
numeric values of the error codes are **not** part of the API
contract. Switch on enumerator names, not on integers.

## Status bands

| Band | When you see it | Common cause |
|---|---|---|
| Argument validation | The caller passed bad input to the API. | NULL pointer, zero-bit buffer, bit width 0 or > 64. |
| Stream / buffer | The bitstream ran past its bounds. | Decoder hit EOS; encoder's output buffer too small. |
| Encoding semantics | The bytes don't represent a valid value of the schema's type. | Malformed varint; unknown enum value; constraint expression false. |
| Resource | The allocator returned NULL. | Bounded arena exhausted; libc OOM. |
| Safety limits | A decode-side hardening limit fired on untrusted input. | Adversarially nested message exceeded the reader's depth ceiling. |

### Argument-validation errors

These are programmer errors. They fire at the public API boundary
when the caller's preconditions aren't met. If your test suite
catches an argument-validation status from a `_read` / `_write`
call, the fix is in your code, not in the bitstream.

### Stream / buffer errors

These are the data-driven errors you'll see in production:

- `ZSERIO_ERR_END_OF_STREAM` — decoder read past the buffer end.
  Either the bitstream is truncated or your decoder is using the
  wrong schema (more fields than the bytes actually encode).
- `ZSERIO_ERR_BUFFER_OVERFLOW` — encoder filled the output buffer.
  Either grow the buffer or stream into a sink that re-allocates.

### Encoding-semantics errors

The bytes are syntactically valid (you didn't run off the buffer)
but don't form a valid instance of the schema's type:

- `ZSERIO_ERR_INVALID_ENCODING` — a varint exceeded its
  schema-permitted width; a varsize had its top bit set in the
  last byte; etc.
- `ZSERIO_ERR_INVALID_ENUM` — the decoded integer is not a
  declared enumerator. The struct field's value is set to the
  out-of-range integer (cast through the enum's underlying type)
  *before* the status is returned, so you can inspect what was
  there if you choose.
- `ZSERIO_ERR_INVALID_TAG` — a union's branch-selector integer is
  outside the declared cases.
- `ZSERIO_ERR_CONSTRAINT_VIOLATED` — a `constraint(expr)` clause
  in the schema evaluated to false against the decoded values.
- `ZSERIO_ERR_OFFSET_MISMATCH` — a `@offset` declaration says the
  next field starts at bit-position N, but the actual decoder
  position differs. Indicates a wire-format / schema mismatch.
- `ZSERIO_ERR_ARRAY_TOO_LARGE` — the decoded array length is
  implausible for the bits remaining in the stream, or the
  element-count × element-size multiplication would overflow
  `size_t`. Protects against crafted input that claims a huge
  array to drive the decoder out of memory. Packed arrays are
  exempt from the remaining-bits plausibility check (their delta
  encoding legitimately packs many elements into few bits) but
  still get the overflow guard.

### Resource errors

`ZSERIO_ERR_OUT_OF_MEMORY` is the **only** error your allocator
can produce, and it's how every backend signals exhaustion. The
runtime catches the NULL return from any of the three allocator
hooks and propagates this status up to the original `_read` call.

The `_read` short-circuits — subsequent fields are not decoded —
but the partially-populated struct is safe to `_free` against the
same allocator.

### Safety-limit errors

- `ZSERIO_ERR_MAX_DEPTH_EXCEEDED` — the decode descended through
  more nested compound levels than the reader's depth ceiling
  allows. Generated compound readers track nesting depth on the
  reader; a recursive schema (linked lists, trees,
  mutually-referential structs) fed an adversarially deep
  bitstream fails with this status instead of exhausting the C
  stack (REQ-FS-003).

  The default ceiling is **1024**. Embedded targets with small
  stacks should lower it before decoding:

  ```c
  zserio_bitstream_reader_t r;
  zserio_bitstream_reader_init(&r, buf, buf_bits);
  zserio_bitstream_reader_set_max_depth(&r, 64);  /* per-message budget */
  ```

  Pick the ceiling from your schema's real nesting depth plus
  margin — for non-recursive schemas the static maximum is known,
  and anything deeper is an attack or corruption, never valid
  data. A failed decode leaves the struct safe to `_free`, same
  as every other read error.

## Propagating with `ZSERIO_TRY`

The runtime and generated code use a single macro to thread status
codes up the call chain:

```c
#define ZSERIO_TRY(expr)                                    \
    do {                                                    \
        const zserio_status_t zserio_try_st = (expr);       \
        if (zserio_try_st != ZSERIO_OK) {                   \
            return zserio_try_st;                           \
        }                                                   \
    } while (0)
```

Usage in caller code:

```c
zserio_status_t parse_message(const uint8_t *buf, size_t buf_len)
{
    zserio_bitstream_reader_t r;
    ZSERIO_TRY(zserio_bitstream_reader_init(&r, buf, buf_len * 8u));

    my_msg_t msg;
    my_msg_init(&msg);

    const zserio_status_t st = my_msg_read(&msg, &r, &alloc);
    if (st != ZSERIO_OK) {
        my_msg_free(&msg, &alloc);  /* safe on partial state */
        return st;
    }

    /* ...handle msg... */
    my_msg_free(&msg, &alloc);
    return ZSERIO_OK;
}
```

`ZSERIO_TRY` is fine in straight-line code where no cleanup is
needed before the early return. When you've allocated something
or set up state that needs unwinding, write the explicit
`if (st != ZSERIO_OK)` form so the cleanup path is visible.

## Errors at boundaries

The runtime makes a strict distinction between the **public API
boundary** (where it validates) and **internal calls** (where it
trusts).

- Functions exposed in `runtime/include/zserio/*.h` validate
  every argument. NULL pointers, zero-size buffers, out-of-range
  parameters all return an `INVALID_*` status.
- Generated `_read` / `_write` functions also validate their
  arguments — they're public API.
- Internal helpers in `runtime/src/` and inside generated `.c`
  files assume their callers have validated. They don't re-check.

This keeps the validation cost paid once per public call instead
of cascading. If you're writing test code that pokes at internal
functions, you own the precondition checking.

## Recovery

The runtime is **fail-fast on the read path** and **safe to clean
up afterwards**:

- Once `_read` returns non-OK, the struct's state is undefined
  in the sense that some fields are populated and some aren't.
- The struct is still **safe to `_free`** against the same
  allocator. `_free` walks every owned-allocation field and
  releases what was populated; unset pointers are NULL so the
  free is a no-op for them.
- You should not consume any of the field values from a failed
  `_read`. If you want to inspect a malformed bitstream, decode
  it field-by-field through the bitstream reader directly rather
  than through the generated `_read`.

There is no "resume" or "retry" path — a failed `_read` is
terminal for that message. Decode it fresh from a new buffer.

## Mapping to upstream zserio C++

The upstream C++ runtime raises one exception type per error
class (`CppRuntimeException`, `ConstraintError`, etc.). The
mapping from those exception subclasses to our status codes is
recorded in `tests/conformance/error_mapping.csv`. Anyone porting
a C++ caller to the C bindings can use that table to translate
their catch blocks into status checks.

## See also

- [Allocators](allocators.md) for `ZSERIO_ERR_OUT_OF_MEMORY` and
  pool sizing.
- [Generated code](generated-code.md) for which functions return
  `zserio_status_t`.
- [`safety-case/`](../safety-case/) for which error codes are
  reachable from which inputs in the FMEA analysis.
