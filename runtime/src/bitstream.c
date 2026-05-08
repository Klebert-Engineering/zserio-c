/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/bitstream.h"

#include <string.h>

/*
 * Phase 1 implementation: byte-walk algorithm. Correctness-first.
 * The bit-cache + mask-table optimization documented in
 * docs/upstream-analysis/04-cpp-runtime-internals.md §5 will replace
 * the loops here when the Phase 6 performance gate (REQ-PF-001)
 * exercises this path.
 */

/* Detect whether `alignment` is a positive power of two. */
static bool is_pow2(size_t alignment)
{
    return (alignment != 0U) && ((alignment & (alignment - 1U)) == 0U);
}

/* Round `value` up to the next multiple of `alignment`. Caller must
 * ensure `is_pow2(alignment)`. */
static size_t round_up_pow2(size_t value, size_t alignment)
{
    return (value + (alignment - 1U)) & ~(alignment - 1U);
}

/* ------------------------------------------------------------------------ */
/* Reader                                                                   */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_bitstream_reader_init(zserio_bitstream_reader_t *bs,
                                             const uint8_t *buffer,
                                             size_t buffer_bit_size)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    /* Empty stream is permitted (any read returns END_OF_STREAM). */
    if ((buffer == NULL) && (buffer_bit_size != 0U)) {
        return ZSERIO_ERR_INVALID_BUFFER;
    }
    /* Guard against bit_index + n overflowing size_t on any read. */
    if (buffer_bit_size > (SIZE_MAX - 64U)) {
        return ZSERIO_ERR_INVALID_BUFFER;
    }

    bs->buffer = buffer;
    bs->buffer_bit_size = buffer_bit_size;
    bs->bit_index = 0U;
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_read_bits(zserio_bitstream_reader_t *bs,
                                           uint8_t num_bits,
                                           uint64_t *out)
{
    if ((bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((num_bits == 0U) || (num_bits > 64U)) {
        return ZSERIO_ERR_INVALID_NUM_BITS;
    }
    if ((bs->bit_index + num_bits) > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }

    uint64_t value = 0U;
    size_t bit_pos = bs->bit_index;
    uint8_t bits_remaining = num_bits;

    while (bits_remaining > 0U) {
        const size_t byte_idx = bit_pos / 8U;
        const uint8_t bit_offset = (uint8_t)(bit_pos % 8U);
        const uint8_t bits_in_byte = (uint8_t)(8U - bit_offset);
        const uint8_t take = (bits_remaining < bits_in_byte) ? bits_remaining : bits_in_byte;
        const uint8_t shift = (uint8_t)(bits_in_byte - take);
        const uint8_t byte_mask = (uint8_t)((1U << take) - 1U);
        const uint8_t extracted = (uint8_t)((bs->buffer[byte_idx] >> shift) & byte_mask);

        value = (value << take) | (uint64_t)extracted;
        bit_pos += take;
        bits_remaining = (uint8_t)(bits_remaining - take);
    }

    bs->bit_index = bit_pos;
    *out = value;
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_read_signed(zserio_bitstream_reader_t *bs,
                                             uint8_t num_bits,
                                             int64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    uint64_t raw = 0U;
    ZSERIO_TRY(zserio_bitstream_read_bits(bs, num_bits, &raw));

    /* Sign-extend if the MSB of the read bits is set. For num_bits == 64
     * the value already occupies all bits and no extension is needed. */
    if (num_bits < 64U) {
        const uint64_t sign_bit = (uint64_t)1U << (num_bits - 1U);
        if ((raw & sign_bit) != 0U) {
            const uint64_t high_mask = ~(((uint64_t)1U << num_bits) - 1U);
            raw |= high_mask;
        }
    }

    /* Bit-pattern reinterpret to int64_t. memcpy is the portable
     * (and MISRA-compliant) idiom for this. */
    int64_t signed_value;
    (void)memcpy(&signed_value, &raw, sizeof(signed_value));
    *out = signed_value;
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_reader_align(zserio_bitstream_reader_t *bs,
                                              size_t alignment)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (!is_pow2(alignment)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    const size_t aligned = round_up_pow2(bs->bit_index, alignment);
    if (aligned > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }
    bs->bit_index = aligned;
    return ZSERIO_OK;
}

size_t zserio_bitstream_reader_position(const zserio_bitstream_reader_t *bs)
{
    return (bs == NULL) ? 0U : bs->bit_index;
}

size_t zserio_bitstream_reader_size(const zserio_bitstream_reader_t *bs)
{
    return (bs == NULL) ? 0U : bs->buffer_bit_size;
}

zserio_status_t zserio_bitstream_reader_seek(zserio_bitstream_reader_t *bs,
                                             size_t bit_position)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (bit_position > bs->buffer_bit_size) {
        return ZSERIO_ERR_INVALID_POSITION;
    }
    bs->bit_index = bit_position;
    return ZSERIO_OK;
}

bool zserio_bitstream_reader_aligned_eof(const zserio_bitstream_reader_t *bs,
                                         size_t alignment)
{
    if ((bs == NULL) || !is_pow2(alignment)) {
        return true;
    }
    /* Saturating arithmetic: if rounding up would overflow size_t,
     * the stream is certainly exhausted. */
    if (bs->bit_index > (SIZE_MAX - (alignment - 1U))) {
        return true;
    }
    const size_t aligned = round_up_pow2(bs->bit_index, alignment);
    return aligned >= bs->buffer_bit_size;
}

/* ------------------------------------------------------------------------ */
/* Writer                                                                   */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_bitstream_writer_init(zserio_bitstream_writer_t *bs,
                                             uint8_t *buffer,
                                             size_t buffer_capacity_bits)
{
    if ((bs == NULL) || (buffer == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (buffer_capacity_bits == 0U) {
        return ZSERIO_ERR_INVALID_BUFFER;
    }
    if (buffer_capacity_bits > (SIZE_MAX - 64U)) {
        return ZSERIO_ERR_INVALID_BUFFER;
    }

    bs->buffer = buffer;
    bs->buffer_capacity_bits = buffer_capacity_bits;
    bs->bit_index = 0U;

    /* Zero the buffer so partial-byte writes via OR don't see garbage,
     * and so unwritten regions (e.g. alignment padding) read back as zero. */
    const size_t byte_capacity = (buffer_capacity_bits + 7U) / 8U;
    (void)memset(buffer, 0, byte_capacity);
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_write_bits(zserio_bitstream_writer_t *bs,
                                            uint8_t num_bits,
                                            uint64_t value)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((num_bits == 0U) || (num_bits > 64U)) {
        return ZSERIO_ERR_INVALID_NUM_BITS;
    }

    /* Range check: value must fit in num_bits unsigned bits. */
    if (num_bits < 64U) {
        const uint64_t max_val = ((uint64_t)1U << num_bits) - 1U;
        if (value > max_val) {
            return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
        }
    }

    if ((bs->bit_index + num_bits) > bs->buffer_capacity_bits) {
        return ZSERIO_ERR_BUFFER_OVERFLOW;
    }

    size_t bit_pos = bs->bit_index;
    uint8_t bits_remaining = num_bits;

    while (bits_remaining > 0U) {
        const size_t byte_idx = bit_pos / 8U;
        const uint8_t bit_offset = (uint8_t)(bit_pos % 8U);
        const uint8_t bits_in_byte = (uint8_t)(8U - bit_offset);
        const uint8_t take = (bits_remaining < bits_in_byte) ? bits_remaining : bits_in_byte;
        const uint8_t shift = (uint8_t)(bits_in_byte - take);

        const uint64_t source_mask = ((uint64_t)1U << take) - 1U;
        const uint64_t bits_to_write = (value >> (bits_remaining - take)) & source_mask;
        const uint8_t  byte_mask = (uint8_t)(((1U << take) - 1U) << shift);

        bs->buffer[byte_idx] = (uint8_t)((bs->buffer[byte_idx] & (uint8_t)~byte_mask)
                                         | (uint8_t)((uint8_t)bits_to_write << shift));

        bit_pos += take;
        bits_remaining = (uint8_t)(bits_remaining - take);
    }

    bs->bit_index = bit_pos;
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_write_signed(zserio_bitstream_writer_t *bs,
                                              uint8_t num_bits,
                                              int64_t value)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((num_bits == 0U) || (num_bits > 64U)) {
        return ZSERIO_ERR_INVALID_NUM_BITS;
    }

    /* Range check: value must fit in [-(2^(N-1)), 2^(N-1) - 1] for num_bits < 64;
     * any int64_t value is in range when num_bits == 64. */
    if (num_bits < 64U) {
        const int64_t max_val = (int64_t)(((uint64_t)1U << (num_bits - 1U)) - 1U);
        const int64_t min_val = -max_val - 1;
        if ((value > max_val) || (value < min_val)) {
            return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
        }
    }

    /* Reinterpret to unsigned bit pattern, then mask to the low num_bits. */
    uint64_t raw;
    (void)memcpy(&raw, &value, sizeof(raw));
    if (num_bits < 64U) {
        raw &= ((uint64_t)1U << num_bits) - 1U;
    }

    return zserio_bitstream_write_bits(bs, num_bits, raw);
}

zserio_status_t zserio_bitstream_writer_align(zserio_bitstream_writer_t *bs,
                                              size_t alignment)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (!is_pow2(alignment)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    const size_t aligned = round_up_pow2(bs->bit_index, alignment);
    if (aligned > bs->buffer_capacity_bits) {
        return ZSERIO_ERR_BUFFER_OVERFLOW;
    }
    /* Padding remains zero because the buffer was zeroed at init. */
    bs->bit_index = aligned;
    return ZSERIO_OK;
}

size_t zserio_bitstream_writer_position(const zserio_bitstream_writer_t *bs)
{
    return (bs == NULL) ? 0U : bs->bit_index;
}

zserio_status_t zserio_bitstream_writer_seek(zserio_bitstream_writer_t *bs,
                                             size_t bit_position)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (bit_position > bs->buffer_capacity_bits) {
        return ZSERIO_ERR_INVALID_POSITION;
    }
    bs->bit_index = bit_position;
    return ZSERIO_OK;
}
