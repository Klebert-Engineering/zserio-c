/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * libFuzzer harness for the runtime decode path, exercised through
 * the generated codec for `builtin_types/all_builtin_types`. That
 * schema is chosen because it carries every fixed-width primitive,
 * the four varint families, and the full bit:N family — the
 * majority of the runtime's read entry points are reached by one
 * decode of an arbitrary byte string.
 *
 * Each iteration: feed `data[0..size)` as a wire payload, call
 * `_read`, free, repeat. libFuzzer's ASan / UBSan instrumentation
 * surfaces buffer-overruns, leaks, signed-overflow / shift / unaligned
 * traps. A clean run is the per-commit evidence behind the safety
 * case's "decode path is fuzzed" claim.
 *
 * Verifies: REQ-FS-006
 */

#include <stddef.h>
#include <stdint.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "builtin_types/all_builtin_types/all_built_in_types.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* Cap input size — well above what the schema's outer envelope
     * fits in, but bounded so libFuzzer's mutator doesn't waste
     * cycles in single-iteration MB-scale inputs. The runtime's own
     * end-of-stream check (driven by the buffer_bit_size we hand it
     * here) is what bounds any length-prefix the input encodes. */
    if (size > 4096U) {
        return 0;
    }

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, data, size * 8U);

    builtin_types_all_builtin_types_all_built_in_types_t out;
    builtin_types_all_builtin_types_all_built_in_types_init(&out);

    const zserio_allocator_t alloc = zserio_allocator_libc();
    (void)builtin_types_all_builtin_types_all_built_in_types_read(&out, &r, &alloc);

    /* Always free, including on read failure — the runtime contracts
     * `_free` to be safe over partial-state structs (all `_init`s
     * zero the storage, all `_free`s NULL-check pointer members). */
    builtin_types_all_builtin_types_all_built_in_types_free(&out, &alloc);
    return 0;
}
