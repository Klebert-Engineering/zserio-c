/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
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
 * Same format as an external conformance-oracle decoder emits. The
 * two traces are intended to be diffed to localize wire-format
 * divergence field-by-field rather than guessing from end-state
 * error codes.
 *
 * Build with -DZSERIO_TRACE_ENABLED, set ZSERIO_TRACE to a writable
 * path, then call zsc_trace_set_enabled(true) at the point where you
 * want tracing to start (typically: right before the region of the
 * decode under investigation).
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

/* zserio_bitstream_mask_table[i] = (1 << i) - 1 for i in [0, 64],
 * with [64] = UINT64_MAX (since (1 << 64) is undefined in C).
 * Declared `extern` in bitstream.h so the inline read_bits_impl
 * defined there can reach the table; the .h reaching across the
 * TU boundary is the whole point of exposing it. */
const uint64_t zserio_bitstream_mask_table[65] = {
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

/* The hot read path lives entirely in bitstream.h:
 *   - zserio_bitstream_parse64_be   (8-byte BE load)
 *   - zserio_bitstream_load_cache_next  (cache refill)
 *   - zserio_bitstream_read_bits_impl   (cache extract)
 * Inlining all three means a caller's translation unit sees the
 * full fast path without crossing a TU boundary or needing LTO. */

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
    bs->depth = 0U;
    bs->max_depth = ZSERIO_BITSTREAM_DEFAULT_MAX_DEPTH;
    return ZSERIO_OK;
}

zserio_status_t zserio_bitstream_reader_enter(zserio_bitstream_reader_t *bs)
{
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (bs->depth >= bs->max_depth) {
        return ZSERIO_ERR_MAX_DEPTH_EXCEEDED;
    }
    bs->depth += 1U;
    return ZSERIO_OK;
}

void zserio_bitstream_reader_leave(zserio_bitstream_reader_t *bs)
{
    if ((bs != NULL) && (bs->depth > 0U)) {
        bs->depth -= 1U;
    }
}

void zserio_bitstream_reader_set_max_depth(zserio_bitstream_reader_t *bs,
                                           size_t max_depth)
{
    if (bs != NULL) {
        bs->max_depth = max_depth;
    }
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

    const uint64_t value = zserio_bitstream_read_bits_impl(bs, num_bits);
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

    /* Smart byte-walk pattern (see
     * docs/upstream-analysis/04-cpp-runtime-internals.md §5). For
     * unaligned writes spanning multiple bytes we split into:
     *   - first partial byte: read-modify-write to preserve already-
     *     populated high bits;
     *   - middle full bytes: straight stores (the buffer is pre-zeroed
     *     and we own these byte positions completely);
     *   - last partial byte: read-modify-write to preserve future-write
     *     low bits (typically zero, but seek-back patterns matter).
     * Byte-aligned writes (bit_index % 8 == 0) skip the first partial
     * branch and run only the straight middle-byte loop. */
    uint8_t rest_bits = num_bits;
    const uint8_t bits_used = (uint8_t)(bs->bit_index & 0x07U);
    uint8_t bits_free = (uint8_t)(8U - bits_used);
    size_t byte_idx = bs->bit_index >> 3U;

    if (rest_bits > bits_free) {
        /* First partial byte: bits_free new bits fit in the trailing
         * low bits_free positions of buffer[byte_idx]; preserve the
         * high bits_used bits. */
        const uint8_t shift_num = (uint8_t)(rest_bits - bits_free);
        const uint8_t preserve_mask = (uint8_t)(0xFFU << bits_free);
        const uint8_t high_bits =
            (uint8_t)((value >> shift_num) & (uint64_t)(0xFFU >> bits_used));
        bs->buffer[byte_idx] = (uint8_t)((bs->buffer[byte_idx] & preserve_mask) | high_bits);
        byte_idx++;
        rest_bits = (uint8_t)(rest_bits - bits_free);

        /* Middle full bytes: each holds 8 contiguous bits of value, no
         * read-modify-write needed because the buffer is pre-zeroed and
         * we own these byte positions. The post-increment is split from
         * the assignment to satisfy MISRA Rule 13.3. */
        while (rest_bits >= 8U) {
            rest_bits = (uint8_t)(rest_bits - 8U);
            bs->buffer[byte_idx] = (uint8_t)((value >> rest_bits) & 0xFFU);
            byte_idx++;
        }
        bits_free = 8U;
    }

    /* Last partial byte: rest_bits new bits land in the high
     * positions of buffer[byte_idx]; preserve the low bits. */
    if (rest_bits > 0U) {
        const uint8_t shift_num = (uint8_t)(bits_free - rest_bits);
        /* rest_bits is provably <= 8 on every feasible path here: it either
         * fell through the `rest_bits >= 8U` loop above, or the
         * `rest_bits > bits_free` branch was skipped, which requires
         * rest_bits <= bits_free <= 8. So `1U << rest_bits` cannot overflow.
         * The analyzer misses the rest_bits <= bits_free relation and explores
         * an infeasible path. */
        /* NOLINTNEXTLINE(clang-analyzer-core.BitwiseShift) */
        const uint8_t mask = (uint8_t)((1U << rest_bits) - 1U);
        const uint8_t preserve_mask = (uint8_t)~(uint8_t)(mask << shift_num);
        const uint8_t new_bits = (uint8_t)(((uint8_t)value & mask) << shift_num);
        bs->buffer[byte_idx] = (uint8_t)((bs->buffer[byte_idx] & preserve_mask) | new_bits);
    }

    bs->bit_index += num_bits;
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
