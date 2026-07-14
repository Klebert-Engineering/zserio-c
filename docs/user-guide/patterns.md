# Patterns

Recipes for common shapes that aren't obvious from the
per-function reference. Pulled from real integrations.

## Round-trip a message

The bidirectional pattern: decode bytes, modify, re-encode. Useful
for filters, transformers, and the cross-validation harness.

```c
zserio_status_t round_trip(const uint8_t *in_buf, size_t in_len,
                           uint8_t *out_buf, size_t out_cap,
                           size_t *out_len,
                           const zserio_allocator_t *alloc)
{
    pkg_msg_t msg;
    pkg_msg_init(&msg);

    /* Decode */
    zserio_bitstream_reader_t r;
    zserio_status_t st = zserio_bitstream_reader_init(&r, in_buf,
                                                      in_len * 8u);
    if (st != ZSERIO_OK) { return st; }

    st = pkg_msg_read(&msg, &r, alloc);
    if (st != ZSERIO_OK) {
        pkg_msg_free(&msg, alloc);   /* safe on partial state */
        return st;
    }

    /* ...modify msg here, e.g. increment a sequence number... */
    msg.sequence_number++;

    /* Re-encode */
    zserio_bitstream_writer_t w;
    st = zserio_bitstream_writer_init(&w, out_buf, out_cap * 8u);
    if (st != ZSERIO_OK) {
        pkg_msg_free(&msg, alloc);
        return st;
    }
    st = pkg_msg_write(&msg, &w);
    if (st == ZSERIO_OK) {
        *out_len = (zserio_bitstream_writer_position(&w) + 7u) / 8u;
    }

    pkg_msg_free(&msg, alloc);
    return st;
}
```

This is the exact shape `tests/xvalidation/c_roundtrip.c` runs
across 183 schemas to verify byte-equal round-trip.

## Reuse a message struct across reads

Decoding many messages into the same struct is the common
hot-path shape. The contract:

```c
pkg_msg_t msg;
pkg_msg_init(&msg);  /* once */

for (size_t i = 0; i < n_messages; ++i) {
    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, msgs[i].bytes,
                                 msgs[i].len * 8u);

    if (pkg_msg_read(&msg, &r, &alloc) != ZSERIO_OK) {
        pkg_msg_free(&msg, &alloc);
        continue;
    }

    /* ...consume msg... */

    pkg_msg_free(&msg, &alloc);  /* before re-reading */
}
```

`_free` returns the struct to its post-`_init` zero state. You
**must** call `_free` between `_read`s into the same struct —
otherwise the second read leaks the first read's allocations.

For tight loops, pair this with an arena allocator so the per-
message `_free` is a no-op and you reset the arena every N
messages:

```c
static uint8_t pool[64 * 1024];
zserio_arena_t arena;
zserio_arena_init(&arena, pool, sizeof pool);
const zserio_allocator_t alloc = zserio_allocator_arena(&arena);

pkg_msg_t msg;
pkg_msg_init(&msg);

for (size_t i = 0; i < n_messages; ++i) {
    if (i % BATCH_SIZE == 0) {
        zserio_arena_reset(&arena);   /* bulk reclaim */
        pkg_msg_init(&msg);            /* clear pointers */
    }

    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, msgs[i].bytes,
                                 msgs[i].len * 8u);
    pkg_msg_read(&msg, &r, &alloc);
    /* ...consume msg... */
    /* no per-message _free needed; arena reset handles it */
}
```

## Stream-decode without buffering the whole message

zserio's wire format is **sequential** — fields are encoded in
source order with no header you can use to skip into the middle.
This means you can't decode field N without decoding fields 1..N-1
first. But you can decode field-by-field by *not* using the
generated `_read` and going to the bitstream reader directly:

```c
zserio_bitstream_reader_t r;
zserio_bitstream_reader_init(&r, buf, buf_bits);

uint32_t message_id;
zserio_bitstream_reader_read_bits(&r, 32, &message_id);

if (message_id != EXPECTED_ID) {
    /* skip the rest of the message without decoding it */
    return ZSERIO_OK;
}

/* if it's the message we wanted, decode the rest normally — but
 * the generated _read assumes start-of-message, so this only works
 * for hand-decoded prefixes followed by hand-decoded suffixes. */
```

This is an advanced pattern. For most use cases the
"decode-then-inspect" flow above is cheaper than it sounds —
zserio's bit-packed format means even large messages decode in
microseconds (`docs/benchmarks/RESULTS.md`).

## Compose schemas

Two schemas reference each other across packages by `import`:

```zserio
// pkg_a/header.zs
package pkg_a;

struct Header { uint32 messageId; };
```

```zserio
// pkg_b/message.zs
package pkg_b;

import pkg_a.*;

struct Message
{
    Header header;
    string body;
};
```

The generator handles cross-package includes by writing relative
`#include "../pkg_a/header.h"` paths. Both packages live under the
same `-c <output-dir>` root.

If your schemas come from different vendor trees with separate
package roots, use multiple `-src` flags:

```sh
zserio_c_gen schemas/main.zs \
    -c build/zserio-out \
    -src schemas \
    -src vendor/external/schemas
```

Imports are resolved against each `-src` root in order; first
match wins.

## Detect partial / truncated input

A `_read` against a buffer shorter than the schema requires
returns `ZSERIO_ERR_END_OF_STREAM`. The struct is partially
populated up to the point of truncation; it's safe to `_free`.

For protocols where you receive bytes in chunks (TCP, UART), the
canonical pattern is:

```c
size_t accumulated = 0;
uint8_t buf[MAX_MSG_BYTES];

while (more_bytes_available()) {
    size_t got = read_into(buf + accumulated,
                           sizeof buf - accumulated);
    if (got == 0) { break; }
    accumulated += got;

    pkg_msg_t msg;
    pkg_msg_init(&msg);
    zserio_bitstream_reader_t r;
    zserio_bitstream_reader_init(&r, buf, accumulated * 8u);

    const zserio_status_t st = pkg_msg_read(&msg, &r, &alloc);
    if (st == ZSERIO_OK) {
        const size_t consumed_bits =
            zserio_bitstream_reader_position(&r);
        const size_t consumed_bytes = (consumed_bits + 7u) / 8u;
        /* ...handle msg... */
        pkg_msg_free(&msg, &alloc);
        memmove(buf, buf + consumed_bytes,
                accumulated - consumed_bytes);
        accumulated -= consumed_bytes;
    } else if (st == ZSERIO_ERR_END_OF_STREAM) {
        /* not enough bytes yet — keep accumulating */
        pkg_msg_free(&msg, &alloc);
    } else {
        /* genuine decode error — drop and resync somehow */
        pkg_msg_free(&msg, &alloc);
        accumulated = 0;
    }
}
```

There is no in-buffer message-length prefix at the zserio layer —
the schema's own size determines how many bytes a message
consumes. If your transport doesn't preserve message boundaries,
you need a framing layer above zserio.

## Size a message before writing

If you need to know the encoded size before allocating the output
buffer:

1. Decode-equivalent size accounting via the generated `_bitsizeof`
   helper:

```c
uint64_t bit_size = pkg_msg_bitsizeof(&msg, 0u);
size_t   buf_size = (size_t)((bit_size + 7u) / 8u);
uint8_t *buf      = malloc(buf_size);
```

2. Or speculate, write, retry on `ZSERIO_ERR_BUFFER_OVERFLOW`:

```c
size_t buf_size = INITIAL_GUESS;
for (;;) {
    uint8_t *buf = realloc(NULL, buf_size);
    zserio_bitstream_writer_t w;
    zserio_bitstream_writer_init(&w, buf, buf_size * 8u);
    const zserio_status_t st = pkg_msg_write(&msg, &w);
    if (st == ZSERIO_OK) { /* done */ break; }
    if (st != ZSERIO_ERR_BUFFER_OVERFLOW) { /* real error */ break; }
    free(buf);
    buf_size *= 2;
}
```

The `_bitsizeof` pattern is preferred — it walks the struct
without writing anything and gives an exact size.

## Compare two decoded messages

There is no generated `_eq` helper. Two messages decoded from the
same bytes are not guaranteed to be `memcmp`-equal (because
zserio_string_t pointers will differ — same bytes, different
heap). For an equality check, encode both and `memcmp` the byte
buffers:

```c
bool messages_equal(const pkg_msg_t *a, const pkg_msg_t *b)
{
    uint8_t  buf_a[MAX], buf_b[MAX];
    zserio_bitstream_writer_t w_a, w_b;
    zserio_bitstream_writer_init(&w_a, buf_a, sizeof buf_a * 8u);
    zserio_bitstream_writer_init(&w_b, buf_b, sizeof buf_b * 8u);
    if (pkg_msg_write(a, &w_a) != ZSERIO_OK) { return false; }
    if (pkg_msg_write(b, &w_b) != ZSERIO_OK) { return false; }
    const size_t n_a = (zserio_bitstream_writer_position(&w_a) + 7u) / 8u;
    const size_t n_b = (zserio_bitstream_writer_position(&w_b) + 7u) / 8u;
    return n_a == n_b && memcmp(buf_a, buf_b, n_a) == 0;
}
```

zserio's encoding is **canonical** — same struct values produce
the same bytes. This is what the xvalidation harness depends on,
and what makes byte-compare a sound equality check.

## See also

- [Generated code](generated-code.md) for the per-type entry
  points.
- [Allocators](allocators.md) for choosing a backing strategy.
- [Error handling](error-handling.md) for the full status taxonomy.
