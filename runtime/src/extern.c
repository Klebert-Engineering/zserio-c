/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/extern.h"

#include <string.h>

#include "zserio/varint.h"

/*
 * `extern` is a varsize bit-length followed by that many raw bits at
 * the current bit position (not byte-aligned). The view path is O(1):
 * we just record the source buffer, the bit-within-byte offset, and
 * the length, then advance the cursor. The materialize path copies the
 * bits into a fresh buffer aligned at byte 0 bit 0.
 */

#define ZSERIO_EXTERN_MAX_BITS ((uint32_t)0x7FFFFFFFU)

zserio_status_t zserio_read_extern_view(zserio_bitstream_reader_t *bs,
                                        zserio_bit_view_t *out)
{
    if ((bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    const size_t saved_index = bs->bit_index;

    uint32_t bit_length = 0U;
    ZSERIO_TRY(zserio_read_varsize(bs, &bit_length));

    const size_t remaining_bits = bs->buffer_bit_size - bs->bit_index;
    if ((size_t)bit_length > remaining_bits) {
        bs->bit_index = saved_index;
        return ZSERIO_ERR_END_OF_STREAM;
    }

    if (bit_length == 0U) {
        out->base = NULL;
        out->bit_offset = 0U;
        out->bit_length = 0U;
        return ZSERIO_OK;
    }

    const size_t base_byte = bs->bit_index / 8U;
    const size_t bit_off = bs->bit_index % 8U;

    out->base = bs->buffer + base_byte;
    out->bit_offset = bit_off;
    out->bit_length = (size_t)bit_length;

    bs->bit_index += (size_t)bit_length;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_extern(zserio_bitstream_reader_t *bs,
                                   const zserio_allocator_t *alloc,
                                   zserio_extern_t *out)
{
    if ((bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((alloc == NULL) || (alloc->vtable == NULL)
            || (alloc->vtable->alloc == NULL) || (alloc->vtable->free == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    uint32_t bit_length = 0U;
    ZSERIO_TRY(zserio_read_varsize(bs, &bit_length));

    if (bit_length == 0U) {
        out->bytes = NULL;
        out->bit_length = 0U;
        return ZSERIO_OK;
    }

    const size_t remaining_bits = bs->buffer_bit_size - bs->bit_index;
    if ((size_t)bit_length > remaining_bits) {
        return ZSERIO_ERR_END_OF_STREAM;
    }

    const size_t byte_count = ((size_t)bit_length + 7U) / 8U;
    uint8_t *const dst = (uint8_t *)alloc->vtable->alloc(alloc->userdata,
                                                         byte_count, 1U);
    if (dst == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }
    (void)memset(dst, 0, byte_count);

    /* Copy full bytes via byte-by-byte read_bits (handles bit-misalignment). */
    const size_t full_bytes = (size_t)bit_length / 8U;
    for (size_t i = 0U; i < full_bytes; i++) {
        uint64_t b = 0U;
        const zserio_status_t st = zserio_bitstream_read_bits(bs, 8U, &b);
        if (st != ZSERIO_OK) {
            alloc->vtable->free(alloc->userdata, dst, byte_count, 1U);
            return st;
        }
        dst[i] = (uint8_t)b;
    }

    /* Partial trailing byte, if any. The trailing bits are MSB-aligned
     * within the last byte; the unused low bits stay zero. */
    const size_t trailing_bits = (size_t)bit_length - (full_bytes * 8U);
    if (trailing_bits > 0U) {
        uint64_t b = 0U;
        const zserio_status_t st = zserio_bitstream_read_bits(bs, (uint8_t)trailing_bits, &b);
        if (st != ZSERIO_OK) {
            alloc->vtable->free(alloc->userdata, dst, byte_count, 1U);
            return st;
        }
        dst[full_bytes] = (uint8_t)(b << (8U - trailing_bits));
    }

    out->bytes = dst;
    out->bit_length = (size_t)bit_length;
    return ZSERIO_OK;
}

void zserio_extern_free(zserio_extern_t *e, const zserio_allocator_t *alloc)
{
    if (e == NULL) {
        return;
    }
    if ((e->bytes != NULL) && (alloc != NULL) && (alloc->vtable != NULL)
            && (alloc->vtable->free != NULL)) {
        const size_t byte_count = (e->bit_length + 7U) / 8U;
        alloc->vtable->free(alloc->userdata, e->bytes, byte_count, 1U);
    }
    e->bytes = NULL;
    e->bit_length = 0U;
}

zserio_status_t zserio_write_extern(zserio_bitstream_writer_t *bs,
                                    zserio_bit_view_t v)
{
    if (v.bit_offset > 7U) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((v.bit_length > 0U) && (v.base == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (v.bit_length > (size_t)ZSERIO_EXTERN_MAX_BITS) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }

    ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)v.bit_length));

    if (v.bit_length == 0U) {
        return ZSERIO_OK;
    }

    /* Use a temporary reader over the source buffer to extract bits
     * cleanly regardless of alignment. */
    zserio_bitstream_reader_t src;
    const size_t total_src_bits = v.bit_offset + v.bit_length;
    ZSERIO_TRY(zserio_bitstream_reader_init(&src, v.base, total_src_bits));
    ZSERIO_TRY(zserio_bitstream_reader_seek(&src, v.bit_offset));

    size_t remaining = v.bit_length;
    while (remaining > 0U) {
        const uint8_t take = (remaining >= 64U) ? 64U : (uint8_t)remaining;
        uint64_t chunk = 0U;
        ZSERIO_TRY(zserio_bitstream_read_bits(&src, take, &chunk));
        ZSERIO_TRY(zserio_bitstream_write_bits(bs, take, chunk));
        remaining -= take;
    }
    return ZSERIO_OK;
}
