/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include "zserio/string.h"

#include <string.h>

#include "zserio/varint.h"

/*
 * Strings on the wire are a `varsize` byte length followed by raw bytes
 * at the current bit position. The byte payload is byte-aligned exactly
 * when the cursor at the start of the field is byte-aligned (varsize is
 * itself read in 8-bit chunks, preserving alignment).
 *
 * Two read variants:
 * - View: zero-copy, requires byte-aligned entry. Errors with
 *   ZSERIO_ERR_INVALID_ARGUMENT otherwise without advancing the cursor.
 * - Materialize: always works, allocates via the caller's vtable and
 *   does the byte-by-byte copy that handles bit-misalignment naturally.
 */

/* Maximum string length expressible by varsize (also the limit we accept
 * for write). 2^31 - 1 bytes; 2 GB. */
#define ZSERIO_STRING_MAX_LENGTH ((uint32_t)0x7FFFFFFFU)

zserio_string_view_t zserio_string_view_from_cstr(const char *cstr)
{
    zserio_string_view_t v;
    if (cstr == NULL) {
        v.bytes = NULL;
        v.length = 0U;
    } else {
        v.bytes = cstr;
        v.length = strlen(cstr);
    }
    return v;
}

zserio_status_t zserio_read_string_view(zserio_bitstream_reader_t *bs,
                                        zserio_string_view_t *out)
{
    if ((bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    /* Refuse if the cursor is not byte-aligned at entry. The cursor is
     * not modified — caller can fall back to the materializing read. */
    if ((bs->bit_index % 8U) != 0U) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    /* Snapshot in case the buffer length check below fails. */
    const size_t saved_index = bs->bit_index;

    uint32_t length = 0U;
    ZSERIO_TRY(zserio_read_varsize(bs, &length));

    /* After varsize read the cursor is still byte-aligned (varsize reads
     * full bytes). Check that the buffer holds `length` more bytes. */
    if (length != 0U) {
        const size_t remaining_bits = bs->buffer_bit_size - bs->bit_index;
        if ((size_t)length > (remaining_bits / 8U)) {
            bs->bit_index = saved_index;
            return ZSERIO_ERR_END_OF_STREAM;
        }
        out->bytes = (const char *)(bs->buffer + (bs->bit_index / 8U));
    } else {
        out->bytes = NULL;
    }
    out->length = (size_t)length;
    bs->bit_index += (size_t)length * 8U;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_string(zserio_bitstream_reader_t *bs,
                                   const zserio_allocator_t *alloc,
                                   zserio_string_t *out)
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

    /* Bound check before allocation to avoid OOM-amplifying malformed input
     * (REQ-FS-005 spirit: the upstream reader applies maxArrayPrealloc;
     *  here the bound is the buffer's own size, which is finite by
     *  construction). */
    const size_t remaining_bits = bs->buffer_bit_size - bs->bit_index;
    if ((size_t)length > (remaining_bits / 8U)) {
        return ZSERIO_ERR_END_OF_STREAM;
    }

    char *const dst = (char *)alloc->vtable->alloc(alloc->userdata,
                                                   (size_t)length, 1U);
    if (dst == NULL) {
        return ZSERIO_ERR_OUT_OF_MEMORY;
    }

    if (((bs->bit_index & (size_t)0x7U) == 0U)
            && ((bs->cache_bits & (uint8_t)0x7U) == 0U)) {
        /* Fast path: byte-aligned stream + whole-byte cache. Same
         * shape as zserio_read_extern's fast path. Drain whole-byte
         * chunks out of the cache, then memcpy the rest from the
         * source buffer. The profile showed 16 strings per NDS Live
         * tile at ~110 ns each — 47% of the per-tile decode time —
         * before this shortcut. */
        size_t i = 0U;
        while ((bs->cache_bits >= 8U) && (i < (size_t)length)) {
            bs->cache_bits = (uint8_t)(bs->cache_bits - 8U);
            dst[i] = (char)(uint8_t)(bs->bit_cache >> bs->cache_bits);
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
        /* Slow path: byte-by-byte for bit-misaligned starts. Uses
         * the inline `_checked` helper so the cache state stays in
         * registers across consecutive byte reads; the whole-string
         * bound check above already guarantees in-range, so the
         * helper's per-byte bound check never trips here. */
        for (size_t i = 0U; i < (size_t)length; i++) {
            uint64_t byte = 0U;
            const zserio_status_t st = zserio_bitstream_read_bits_checked(bs, 8U, &byte);
            if (st != ZSERIO_OK) { /* LCOV_EXCL_START — unreachable: the
                                    * bound check above guarantees the loop
                                    * stays within buffer_bit_size. */
                alloc->vtable->free(alloc->userdata, dst, (size_t)length, 1U);
                return st;
            } /* LCOV_EXCL_STOP */
            dst[i] = (char)(uint8_t)byte;
        }
    }

    out->bytes = dst;
    out->length = (size_t)length;
    return ZSERIO_OK;
}

void zserio_string_free(zserio_string_t *s, const zserio_allocator_t *alloc)
{
    if (s == NULL) {
        return;
    }
    if ((s->bytes != NULL) && (alloc != NULL) && (alloc->vtable != NULL)
            && (alloc->vtable->free != NULL)) {
        alloc->vtable->free(alloc->userdata, s->bytes, s->length, 1U);
    }
    s->bytes = NULL;
    s->length = 0U;
}

zserio_status_t zserio_write_string(zserio_bitstream_writer_t *bs,
                                    zserio_string_view_t s)
{
    if (s.length > ZSERIO_STRING_MAX_LENGTH) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }
    if ((s.length > 0U) && (s.bytes == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    ZSERIO_TRY(zserio_write_varsize(bs, (uint32_t)s.length));

    for (size_t i = 0U; i < s.length; i++) {
        const uint64_t byte = (uint64_t)(uint8_t)s.bytes[i];
        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 8U, byte));
    }
    return ZSERIO_OK;
}
