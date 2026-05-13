/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/bitstream.h"

#include <string.h>

/*
 * Per-primitive-read trace facility. Built only when
 * `ZSERIO_TRACE_ENABLED` is defined at compile time — production
 * builds compile this block out entirely so the runtime stays free
 * of <stdio.h>, getenv, fopen, fprintf (all MISRA-banned).
 *
 * Enabled at runtime via env var
 *   ZSERIO_TRACE=<path>
 * Each call to zserio_bitstream_read_bits / read_signed emits one line:
 *   <pos_after> <op> <nbits> <value>
 * Same format as the cpp oracle (conformance/ndslive/cpp). The two
 * traces are intended to be diffed to localise wire-format divergence
 * field-by-field rather than guessing from end-state error codes.
 *
 * Build with -DZSERIO_TRACE_ENABLED, set ZSERIO_TRACE to a writable
 * path, then call zsc_trace_set_enabled(true) at the point where you
 * want tracing to start (typically: after the SmartLayerTile is
 * decoded and we're about to decode a specific layer payload).
 */
#ifdef ZSERIO_TRACE_ENABLED
/* cppcheck-suppress misra-c2012-21.6 ; production builds do not
 * define ZSERIO_TRACE_ENABLED so this include is never compiled in;
 * cppcheck's misra addon scans include directives regardless of
 * preprocessor state (false positive). */
#include <stdio.h>
#include <stdlib.h>
static FILE *s_zsc_trace_fp = NULL;
static bool s_zsc_trace_inited = false;
static bool s_zsc_trace_enabled = false;
static void zsc_trace_init(void)
{
    if (s_zsc_trace_inited)
    {
        return;
    }
    s_zsc_trace_inited = true;
    const char *path = getenv("ZSERIO_TRACE");
    if ((path != NULL) && (*path != '\0'))
    {
        s_zsc_trace_fp = fopen(path, "w");
    }
}
void zsc_trace_set_enabled(bool enabled)
{
    zsc_trace_init();
    s_zsc_trace_enabled = enabled;
}
static void zsc_trace(const char *op, unsigned nbits, size_t pos_after, unsigned long long value)
{
    if (!s_zsc_trace_inited)
    {
        zsc_trace_init();
    }
    if ((s_zsc_trace_fp != NULL) && s_zsc_trace_enabled)
    {
        (void)fprintf(s_zsc_trace_fp, "%zu %s %u %llu\n", pos_after, op, nbits, value);
    }
}
#else
void zsc_trace_set_enabled(bool enabled)
{
    (void)enabled;
}
#define zsc_trace(op, nbits, pos_after, value) ((void)0)
#endif /* ZSERIO_TRACE_ENABLED */

/*
 * Hot-path implementation: bit-cache + mask-table pattern documented in
 * docs/upstream-analysis/04-cpp-runtime-internals.md §5. The reader keeps a
 * 64-bit right-justified cache; reads under `cache_bits` are a shift + mask;
 * larger reads consume the cache, refill from the buffer, and consume the
 * remainder.
 *
 * Bytes load from the buffer big-endian into the cache (zserio wire is MSB-
 * first). The refill is byte-aligned by invariant: at refill time, `bit_index`
 * is always a multiple of 8 because the cache always drains exactly the
 * preceding refill's bit count, and refills only happen in 8-bit chunks.
 */

/* MASK_TABLE[i] = (1 << i) - 1 for i in [0, 64], with [64] = UINT64_MAX
 * (since (1 << 64) is undefined in C). Indexed by the bit width of an
 * extract; the table is hot-cache-resident at ~520 bytes. */
static const uint64_t MASK_TABLE[65] = {
    UINT64_C(0x0000000000000000),
    UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000003),
    UINT64_C(0x0000000000000007), UINT64_C(0x000000000000000F),
    UINT64_C(0x000000000000001F), UINT64_C(0x000000000000003F),
    UINT64_C(0x000000000000007F), UINT64_C(0x00000000000000FF),
    UINT64_C(0x00000000000001FF), UINT64_C(0x00000000000003FF),
    UINT64_C(0x00000000000007FF), UINT64_C(0x0000000000000FFF),
    UINT64_C(0x0000000000001FFF), UINT64_C(0x0000000000003FFF),
    UINT64_C(0x0000000000007FFF), UINT64_C(0x000000000000FFFF),
    UINT64_C(0x000000000001FFFF), UINT64_C(0x000000000003FFFF),
    UINT64_C(0x000000000007FFFF), UINT64_C(0x00000000000FFFFF),
    UINT64_C(0x00000000001FFFFF), UINT64_C(0x00000000003FFFFF),
    UINT64_C(0x00000000007FFFFF), UINT64_C(0x0000000000FFFFFF),
    UINT64_C(0x0000000001FFFFFF), UINT64_C(0x0000000003FFFFFF),
    UINT64_C(0x0000000007FFFFFF), UINT64_C(0x000000000FFFFFFF),
    UINT64_C(0x000000001FFFFFFF), UINT64_C(0x000000003FFFFFFF),
    UINT64_C(0x000000007FFFFFFF), UINT64_C(0x00000000FFFFFFFF),
    UINT64_C(0x00000001FFFFFFFF), UINT64_C(0x00000003FFFFFFFF),
    UINT64_C(0x00000007FFFFFFFF), UINT64_C(0x0000000FFFFFFFFF),
    UINT64_C(0x0000001FFFFFFFFF), UINT64_C(0x0000003FFFFFFFFF),
    UINT64_C(0x0000007FFFFFFFFF), UINT64_C(0x000000FFFFFFFFFF),
    UINT64_C(0x000001FFFFFFFFFF), UINT64_C(0x000003FFFFFFFFFF),
    UINT64_C(0x000007FFFFFFFFFF), UINT64_C(0x00000FFFFFFFFFFF),
    UINT64_C(0x00001FFFFFFFFFFF), UINT64_C(0x00003FFFFFFFFFFF),
    UINT64_C(0x00007FFFFFFFFFFF), UINT64_C(0x0000FFFFFFFFFFFF),
    UINT64_C(0x0001FFFFFFFFFFFF), UINT64_C(0x0003FFFFFFFFFFFF),
    UINT64_C(0x0007FFFFFFFFFFFF), UINT64_C(0x000FFFFFFFFFFFFF),
    UINT64_C(0x001FFFFFFFFFFFFF), UINT64_C(0x003FFFFFFFFFFFFF),
    UINT64_C(0x007FFFFFFFFFFFFF), UINT64_C(0x00FFFFFFFFFFFFFF),
    UINT64_C(0x01FFFFFFFFFFFFFF), UINT64_C(0x03FFFFFFFFFFFFFF),
    UINT64_C(0x07FFFFFFFFFFFFFF), UINT64_C(0x0FFFFFFFFFFFFFFF),
    UINT64_C(0x1FFFFFFFFFFFFFFF), UINT64_C(0x3FFFFFFFFFFFFFFF),
    UINT64_C(0x7FFFFFFFFFFFFFFF), UINT64_C(0xFFFFFFFFFFFFFFFF)
};

/* Big-endian 8-byte parser. Bytes load MSB-first to match the wire. */
static inline uint64_t parse64_be(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48)
         | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
         | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16)
         | ((uint64_t)p[6] <<  8) | ((uint64_t)p[7]);
}

/* Refill the bit cache. Precondition: caller has verified the read is in
 * bounds (`bit_index + needed <= buffer_bit_size`) and `cache_bits == 0`.
 * When ≥ 8 bytes remain we load all 64 bits; otherwise we load only the
 * trailing partial-byte chunk and right-shift away the padding bits.
 *
 * `bit_index` is byte-aligned at entry by the cache invariant. */
static inline void load_cache_next(zserio_bitstream_reader_t *bs)
{
    const size_t byte_idx = bs->bit_index >> 3U;
    const size_t remaining = bs->buffer_bit_size - bs->bit_index;
    if (remaining >= 64U) {
        bs->bit_cache = parse64_be(bs->buffer + byte_idx);
        bs->cache_bits = 64U;
    } else {
        /* Build cache one byte at a time from the trailing tail. Each
         * call here loads at most 56 bits (7 bytes) and runs at most
         * once per stream — every preceding refill went through the
         * fast 64-bit path above. */
        const uint8_t valid_bits = (uint8_t)remaining;
        const uint8_t aligned = (uint8_t)((valid_bits + 7U) & (uint8_t)~0x7U);
        const size_t byte_count = (size_t)aligned >> 3U;
        uint64_t v = 0U;
        for (size_t i = 0U; i < byte_count; i++) {
            v = (v << 8) | (uint64_t)bs->buffer[byte_idx + i];
        }
        /* Right-shift the trailing zero-pad off so the cache is right-
         * justified with `valid_bits` low bits valid. */
        if (aligned != valid_bits) {
            v >>= (uint8_t)(aligned - valid_bits);
        }
        bs->bit_cache = v;
        bs->cache_bits = valid_bits;
    }
}

/* Unchecked impl: caller guarantees `num_bits in [1, 64]` and that the
 * read is in bounds. Returns the read value. */
static inline uint64_t read_bits_impl(zserio_bitstream_reader_t *bs, uint8_t num_bits)
{
    uint64_t value = 0U;
    if (bs->cache_bits < num_bits) {
        /* Take the entire cache as the high part of the result. */
        value = bs->bit_cache & MASK_TABLE[bs->cache_bits];
        bs->bit_index += bs->cache_bits;
        num_bits = (uint8_t)(num_bits - bs->cache_bits);
        bs->cache_bits = 0U;
        load_cache_next(bs);
        /* MASK_TABLE[0] == 0 ensures the shift-by-0 case (num_bits == 64
         * after cache drain) produces value == 0, leaving the low 64
         * bits to come entirely from the refilled cache below. */
        if (num_bits < 64U) {
            value <<= num_bits;
        }
    }
    value |= (bs->bit_cache >> (uint8_t)(bs->cache_bits - num_bits)) & MASK_TABLE[num_bits];
    bs->cache_bits = (uint8_t)(bs->cache_bits - num_bits);
    bs->bit_index += num_bits;
    return value;
}

/* Round `value` up to the next multiple of `alignment`. The zserio
 * `align(N):` directive accepts any positive integer (the schema uses
 * align(3), align(5), align(6), align(7) in alignment/bit_alignment),
 * so the implementation cannot assume a power of two — the bitmask
 * trick is only valid in that case. Power-of-two N falls out as a
 * special case of the general formula. */
static bool is_valid_alignment(size_t alignment)
{
    return alignment != 0U;
}

static size_t round_up_to_multiple(size_t value, size_t alignment)
{
    const size_t rem = value % alignment;
    return (rem == 0U) ? value : (value + (alignment - rem));
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
    bs->bit_cache = 0U;
    bs->cache_bits = 0U;
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

    const uint64_t value = read_bits_impl(bs, num_bits);
    *out = value;
    zsc_trace("read", num_bits, bs->bit_index, (unsigned long long)value);
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
        const uint64_t sign_bit = (uint64_t)1U << (uint64_t)(num_bits - 1U);
        if ((raw & sign_bit) != 0U) {
            const uint64_t high_mask = ~(((uint64_t)1U << (uint64_t)num_bits) - 1U);
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

/* Reposition the reader to `target` (must be in bounds). Invalidates the
 * cache and re-aligns to the nearest byte boundary on or below `target`;
 * any sub-byte remainder is consumed via a checked read so the cache is
 * left in the standard fast-path shape. */
static zserio_status_t reposition(zserio_bitstream_reader_t *bs, size_t target)
{
    const size_t aligned = (target >> 3U) << 3U;
    bs->bit_index = aligned;
    bs->bit_cache = 0U;
    bs->cache_bits = 0U;
    const uint8_t skip = (uint8_t)(target - aligned);
    if (skip != 0U) {
        uint64_t discard = 0U;
        return zserio_bitstream_read_bits(bs, skip, &discard);
    }
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_reader_align(zserio_bitstream_reader_t *bs,
                                              size_t alignment)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (!is_valid_alignment(alignment)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    const size_t aligned = round_up_to_multiple(bs->bit_index, alignment);
    if (aligned > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }
    if (aligned == bs->bit_index) {
        return ZSERIO_OK;
    }
    return reposition(bs, aligned);
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
    return reposition(bs, bit_position);
}

bool zserio_bitstream_reader_aligned_eof(const zserio_bitstream_reader_t *bs,
                                         size_t alignment)
{
    if ((bs == NULL) || !is_valid_alignment(alignment)) {
        return true;
    }
    /* Saturating arithmetic: if rounding up would overflow size_t,
     * the stream is certainly exhausted. Defensive — unreachable in
     * practice because reader_init rejects buffers near SIZE_MAX. */
    if (bs->bit_index > (SIZE_MAX - (alignment - 1U))) {
        return true; /* LCOV_EXCL_LINE — defensive overflow guard */
    }
    const size_t aligned = round_up_to_multiple(bs->bit_index, alignment);
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

        const uint64_t source_mask = ((uint64_t)1U << (uint64_t)take) - 1U;
        const uint64_t bits_to_write = (value >> (uint64_t)(bits_remaining - take)) & source_mask;
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
        const int64_t max_val = (int64_t)(((uint64_t)1U << (uint64_t)(num_bits - 1U)) - 1U);
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
    if (!is_valid_alignment(alignment)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    const size_t aligned = round_up_to_multiple(bs->bit_index, alignment);
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
