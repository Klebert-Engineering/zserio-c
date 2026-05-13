/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/bytes.h"

#include <string.h>

#include "zserio/varint.h"

/*
 * `bytes` mirrors `string` exactly on the wire — varsize byte count
 * followed by raw bytes — and the dual API has the same shape: zero-
 * copy view requires byte-aligned source, materializing read handles
 * misalignment by going through read_bits(8) per byte.
 */

#define ZSERIO_BYTES_MAX_LENGTH ((uint32_t)0x7FFFFFFFU)

zserio_status_t zserio_read_bytes_view(zserio_bitstream_reader_t *bs,
                                       zserio_bytes_view_t *out)
{
    if ((bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((bs->bit_index % 8U) != 0U) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    const size_t saved_index = bs->bit_index;

    uint32_t length = 0U;
    ZSERIO_TRY(zserio_read_varsize(bs, &length));

    if (length != 0U) {
        const size_t remaining_bits = bs->buffer_bit_size - bs->bit_index;
        if ((size_t)length > (remaining_bits / 8U)) {
            bs->bit_index = saved_index;
            return ZSERIO_ERR_END_OF_STREAM;
        }
        out->base = bs->buffer + (bs->bit_index / 8U);
    } else {
        out->base = NULL;
    }
    out->length = (size_t)length;
    bs->bit_index += (size_t)length * 8U;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_bytes(zserio_bitstream_reader_t *bs,
                                  const zserio_allocator_t *alloc,
                                  zserio_bytes_t *out)
{
    if ((bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((alloc == NULL) || (alloc->vtable == NULL)
            || (alloc->vtable->alloc == NULL) || (alloc->vtable->free == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    uint32_t length = 0U;
    ZSERIO_TRY(zserio_read_varsize(bs, &length));

    if (length == 0U) {
        out->bytes = NULL;
        out->length = 0U;
        return ZSERIO_OK;
    }

    const size_t remaining_bits = bs->buffer_bit_size - bs->bit_index;
    if ((size_t)length > (remaining_bits / 8U)) {
        return ZSERIO_ERR_END_OF_STREAM;
    }

    uint8_t *const dst = (uint8_t *)alloc->vtable->alloc(alloc->userdata,
                                                         (size_t)length, 1U);
    if (dst == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    if (((bs->bit_index & (size_t)0x7U) == 0U)
            && ((bs->cache_bits & (uint8_t)0x7U) == 0U)) {
        /* Fast path mirror of zserio_read_extern / zserio_read_string:
         * byte-aligned stream + whole-byte cache → drain cache then
         * memcpy the rest from the buffer. */
        size_t i = 0U;
        while ((bs->cache_bits >= 8U) && (i < (size_t)length)) {
            bs->cache_bits = (uint8_t)(bs->cache_bits - 8U);
            dst[i] = (uint8_t)(bs->bit_cache >> bs->cache_bits);
            bs->bit_index += 8U;
            i++;
        }
        if (i < (size_t)length) {
            const size_t src_byte = bs->bit_index >> 3U;
            const size_t to_copy = (size_t)length - i;
            (void)memcpy(&dst[i], &bs->buffer[src_byte], to_copy);
            bs->bit_index += to_copy * 8U;
        }
    } else {
        for (size_t i = 0U; i < (size_t)length; i++) {
            uint64_t byte = 0U;
            const zserio_status_t st = zserio_bitstream_read_bits(bs, 8U, &byte);
            if (st != ZSERIO_OK) { /* LCOV_EXCL_START — unreachable: the
                                    * bound check above guarantees
                                    * bit_index + length*8 <= buffer size. */
                alloc->vtable->free(alloc->userdata, dst, (size_t)length, 1U);
                return st;
            } /* LCOV_EXCL_STOP */
            dst[i] = (uint8_t)byte;
        }
    }

    out->bytes = dst;
    out->length = (size_t)length;
    return ZSERIO_OK;
}

void zserio_bytes_free(zserio_bytes_t *b, const zserio_allocator_t *alloc)
{
    if (b == NULL) {
        return;
    }
    if ((b->bytes != NULL) && (alloc != NULL) && (alloc->vtable != NULL)
            && (alloc->vtable->free != NULL)) {
        alloc->vtable->free(alloc->userdata, b->bytes, b->length, 1U);
    }
    b->bytes = NULL;
    b->length = 0U;
}

zserio_status_t zserio_write_bytes(zserio_bitstream_writer_t *bs,
                                   zserio_bytes_view_t v)
{
    if (v.length > (size_t)ZSERIO_BYTES_MAX_LENGTH) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }
    if ((v.length > 0U) && (v.base == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)v.length));

    for (size_t i = 0U; i < v.length; i++) {
        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 8U, (uint64_t)v.base[i]));
    }
    return ZSERIO_OK;
}
