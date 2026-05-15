/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * libFuzzer harness for the runtime decode path exercised through
 * `functions/structure_string`. That schema is chosen because it
 * exercises:
 *   - the string allocator path (varsize length-prefix + heap
 *     allocation + the bit-misaligned / byte-aligned read variants),
 *   - nested-compound dispatch (TestStructure -> StringPool -> field),
 *   - function-typed accessors (which lower to inline accessors over
 *     the parsed fields — no runtime evaluation here, but the parsing
 *     of the surrounding compound is what we want to stress).
 *
 * A clean run combined with the builtin-types harness covers the
 * runtime's two main decode-side allocation patterns (none vs.
 * string-array) and the nested-compound dispatch.
 *
 * Verifies: REQ-FS-006
 */

#include <stddef.h>
#include <stdint.h>

#include <zserio/allocator.h>
#include <zserio/bitstream.h>
#include <zserio/status.h>

#include "functions/structure_string/test_structure.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 4096U) {
        return 0;
    }

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, data, size * 8U);

    functions_structure_string_test_structure_t out;
    functions_structure_string_test_structure_init(&out);

    const zserio_allocator_t alloc = zserio_allocator_libc();
    (void)functions_structure_string_test_structure_read(&out, &r, &alloc);
    functions_structure_string_test_structure_free(&out, &alloc);
    return 0;
}
