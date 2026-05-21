/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSERIO_BITSTREAM_H_
#define ZSERIO_BITSTREAM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "zserio/status.h"

/**
 * @file
 * Bit-level stream reader and writer.
 *
 * The reader borrows a const byte buffer and reads big-endian, MSB-first
 * fields of 1..64 bits. The writer borrows a writable byte buffer and
 * writes the same way. Neither owns its buffer; the caller is
 * responsible for keeping the buffer alive across all operations on
 * the stream.
 *
 * The state structs are exposed in this header so callers may
 * stack-allocate them. Their fields are an implementation detail —
 * do not access them directly outside this module.
 */

/* ------------------------------------------------------------------------ */
/* Reader                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct zserio_bitstream_reader {
    const uint8_t *buffer;            /**< Borrowed; not owned. */
    size_t         buffer_bit_size;   /**< Total bits the buffer represents. */
    size_t         bit_index;         /**< Current read position in bits.
                                       *   When `cache_bits == 0`, `bit_index`
                                       *   is byte-aligned and points at the
                                       *   next byte to load into the cache.
                                       *   While `cache_bits > 0`, `bit_index`
                                       *   is the LOGICAL stream position the
                                       *   caller observes; the buffer has
                                       *   already been read up to
                                       *   `bit_index + cache_bits`. */
    uint64_t       bit_cache;         /**< Right-justified cache of pre-read
                                       *   bits. The low `cache_bits` bits are
                                       *   the unread part of the stream.
                                       *   Implementation detail. */
    uint8_t        cache_bits;        /**< Number of valid bits in `bit_cache`,
                                       *   in [0, 64]. Implementation detail. */
} zserio_bitstream_reader_t;

/**
 * Initialize a reader over `buffer` with `buffer_bit_size` valid bits.
 *
 * `buffer` may be NULL only when `buffer_bit_size == 0` (an empty
 * stream). `buffer_bit_size` MUST be at most `(SIZE_MAX / 8) * 8` so
 * that `bit_index + n` does not overflow on any valid read.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` is NULL
 * - `ZSERIO_ERR_INVALID_BUFFER` if buffer/bit-size pair is inconsistent
 */
zserio_status_t zserio_bitstream_reader_init(zserio_bitstream_reader_t *bs,
                                             const uint8_t *buffer,
                                             size_t buffer_bit_size);

/**
 * Read `num_bits` bits as an unsigned integer.
 *
 * `num_bits` MUST be in [1, 64]. Output is written to `*out` only on
 * success.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` or `out` is NULL
 * - `ZSERIO_ERR_INVALID_NUM_BITS` if `num_bits` is 0 or > 64
 * - `ZSERIO_ERR_END_OF_STREAM` if not enough bits remain
 */
zserio_status_t zserio_bitstream_read_bits(zserio_bitstream_reader_t *bs,
                                           uint8_t num_bits,
                                           uint64_t *out);

/**
 * Mask table for bit-cache fast-path extracts.
 *
 * `zserio_bitstream_mask_table[i] = (1 << i) - 1` for i in [0, 63];
 * [64] = UINT64_MAX since (1 << 64) is undefined in C. Defined in
 * bitstream.c. Exposed here so the inline fast path below can use
 * it without crossing a translation-unit boundary.
 *
 * Implementation detail. Not part of the public API contract; do
 * not depend on the symbol name from outside the runtime.
 */
extern const uint64_t zserio_bitstream_mask_table[65];

/**
 * Big-endian 8-byte parser. Bytes load MSB-first to match the
 * zserio wire format. Implementation detail; exposed so the inline
 * cache-refill below stays self-contained.
 */
static inline uint64_t zserio_bitstream_parse64_be(const uint8_t *p)
{
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48)
         | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32)
         | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16)
         | ((uint64_t)p[6] <<  8) | ((uint64_t)p[7]);
}

/**
 * Refill the bit cache from `bs->bit_index`. Precondition: caller
 * has verified the read is in bounds and `bs->cache_bits == 0`.
 * `bs->bit_index` is byte-aligned at entry by the cache invariant.
 *
 * Inlined so callers don't pay a function call on the cache-miss
 * path. The full-64-bit branch is the common one and is a straight
 * 8-byte load; the partial-tail branch fires at most once per
 * stream (the very end of the buffer).
 *
 * Implementation detail. Used by the inline fast path below.
 */
static inline void zserio_bitstream_load_cache_next(zserio_bitstream_reader_t *bs)
{
    const size_t byte_idx = bs->bit_index >> 3U;
    const size_t remaining = bs->buffer_bit_size - bs->bit_index;
    if (remaining >= 64U) {
        bs->bit_cache = zserio_bitstream_parse64_be(&bs->buffer[byte_idx]);
        bs->cache_bits = 64U;
    } else {
        /* Build cache one byte at a time from the trailing tail. */
        const uint8_t valid_bits = (uint8_t)remaining;
        const uint8_t aligned = (uint8_t)((valid_bits + 7U) & (uint8_t)~0x7U);
        const size_t byte_count = (size_t)aligned >> 3U;
        uint64_t v = 0U;
        for (size_t i = 0U; i < byte_count; i++) {
            v = (v << 8) | (uint64_t)bs->buffer[byte_idx + i];
        }
        if (aligned != valid_bits) {
            v >>= (uint8_t)(aligned - valid_bits);
        }
        bs->bit_cache = v;
        bs->cache_bits = valid_bits;
    }
}

/* Branch-prediction hint. `__builtin_expect` is a gcc / clang
 * extension; under MISRA we treat it as a no-op so we don't drag a
 * Rule 1.2 (language extensions) deviation across every read site.
 * The hint is a performance signal, not a correctness one — the
 * code behaves identically with or without it. */
#if defined(__GNUC__) || defined(__clang__)
#  define ZSERIO_BITSTREAM_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define ZSERIO_BITSTREAM_UNLIKELY(x) (x)
#endif

/**
 * Inline bit-cache read fast path.
 *
 * Caller MUST have already verified:
 *   - `bs` and the bit-index are valid;
 *   - `num_bits` is in [1, 64];
 *   - `bs->bit_index + num_bits <= bs->buffer_bit_size`.
 *
 * The public `zserio_bitstream_read_bits` does those checks and
 * then delegates here. Per-width primitive readers in primitives.h
 * inline this directly after their own narrower checks. Both
 * patterns avoid the validate-and-dispatch overhead per primitive
 * read.
 *
 * Implementation detail. Calling this without the preconditions is
 * undefined behaviour.
 */
static inline uint64_t zserio_bitstream_read_bits_impl(
    zserio_bitstream_reader_t *bs, uint8_t num_bits)
{
    uint64_t value = 0U;
    uint8_t remaining = num_bits;
    if (ZSERIO_BITSTREAM_UNLIKELY(bs->cache_bits < remaining)) {
        value = bs->bit_cache & zserio_bitstream_mask_table[bs->cache_bits];
        bs->bit_index += bs->cache_bits;
        remaining = (uint8_t)(remaining - bs->cache_bits);
        bs->cache_bits = 0U;
        zserio_bitstream_load_cache_next(bs);
        if (remaining < 64U) {
            value <<= remaining;
        }
    }
    value |= (bs->bit_cache >> (uint8_t)(bs->cache_bits - remaining))
           & zserio_bitstream_mask_table[remaining];
    bs->cache_bits = (uint8_t)(bs->cache_bits - remaining);
    bs->bit_index += remaining;
    return value;
}

/**
 * Inline bounds-checked read for runtime byte-loops (varint, string slow
 * path, ...) that already validated `bs` and `out` and pass a constant
 * `num_bits` in [1, 64]. The end-of-stream bound check stays; the
 * redundant NULL / range checks the public `zserio_bitstream_read_bits`
 * does are skipped, and crucially the call boundary is removed so
 * `bs->bit_cache` / `cache_bits` / `bit_index` can stay in registers
 * across consecutive byte reads in a tight varint loop.
 *
 * The `ZSERIO_TRACE_ENABLED` debug build falls through to the out-of-
 * line public function so the per-read trace hook still fires for the
 * conformance wire-format diff (see bitstream.c's trace facility).
 *
 * Implementation detail. Calling without the preconditions is UB.
 */
static inline zserio_status_t zserio_bitstream_read_bits_checked(
    zserio_bitstream_reader_t *bs, uint8_t num_bits, uint64_t *out)
{
#ifdef ZSERIO_TRACE_ENABLED
    return zserio_bitstream_read_bits(bs, num_bits, out);
#else
    if ((bs->bit_index + num_bits) > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }
    *out = zserio_bitstream_read_bits_impl(bs, num_bits);
    return ZSERIO_OK;
#endif
}

/**
 * Read `num_bits` bits as a signed integer (two's complement, sign-extended).
 *
 * `num_bits` MUST be in [1, 64]. Output is written to `*out` only on
 * success.
 *
 * Same error returns as `zserio_bitstream_read_bits`.
 */
zserio_status_t zserio_bitstream_read_signed(zserio_bitstream_reader_t *bs,
                                             uint8_t num_bits,
                                             int64_t *out);

/**
 * Inline bounds-checked signed read. Same shape and contract as
 * `zserio_bitstream_read_bits_checked` (preconditions: `bs` / `out`
 * non-NULL, `num_bits` in [1, 64]) — designed for the packed-delta
 * steady-state path where the surrounding `zserio_delta_read` inline
 * has already validated arguments and the per-element read is the
 * inner hot operation. Skipping the call boundary keeps the bit-cache
 * state in registers across consecutive calls.
 *
 * Implementation detail. Calling without the preconditions is UB.
 */
static inline zserio_status_t zserio_bitstream_read_signed_checked(
    zserio_bitstream_reader_t *bs, uint8_t num_bits, int64_t *out)
{
#ifdef ZSERIO_TRACE_ENABLED
    return zserio_bitstream_read_signed(bs, num_bits, out);
#else
    if ((bs->bit_index + num_bits) > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }
    uint64_t raw = zserio_bitstream_read_bits_impl(bs, num_bits);
    if (num_bits < 64U) {
        const uint64_t sign_bit = (uint64_t)1U << (uint64_t)(num_bits - 1U);
        if ((raw & sign_bit) != 0U) {
            const uint64_t high_mask = ~(((uint64_t)1U << (uint64_t)num_bits) - 1U);
            raw |= high_mask;
        }
    }
    int64_t signed_value;
    (void)memcpy(&signed_value, &raw, sizeof(signed_value));
    *out = signed_value;
    return ZSERIO_OK;
#endif
}

/**
 * Advance the read position to the next multiple of `alignment` bits.
 *
 * `alignment` MUST be a power of two. If the current position is
 * already aligned, this is a no-op.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` is NULL or `alignment` is
 *   not a positive power of two
 * - `ZSERIO_ERR_END_OF_STREAM` if alignment would advance past end
 */
zserio_status_t zserio_bitstream_reader_align(zserio_bitstream_reader_t *bs,
                                              size_t alignment);

/**
 * Returns the current read position in bits, or 0 if `bs` is NULL.
 */
size_t zserio_bitstream_reader_position(const zserio_bitstream_reader_t *bs);

/**
 * Returns the buffer's total bit size, or 0 if `bs` is NULL.
 */
size_t zserio_bitstream_reader_size(const zserio_bitstream_reader_t *bs);

/**
 * Set the read position to `bit_position`.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` is NULL
 * - `ZSERIO_ERR_INVALID_POSITION` if `bit_position` exceeds buffer size
 */
zserio_status_t zserio_bitstream_reader_seek(zserio_bitstream_reader_t *bs,
                                             size_t bit_position);

/**
 * Returns true when, after rounding the current position up to the
 * next multiple of `alignment` bits, no bits remain. This is the
 * end-of-stream probe used by `extend`-field decode (see
 * `docs/upstream-analysis/01-schema-language.md` §6.3).
 *
 * Defensive on bad input: returns true (i.e. "yes, EOF") when `bs`
 * is NULL or `alignment` is zero or non-power-of-two. Decoders that
 * see EOF stop reading further extends — the safe-fail mode.
 */
bool zserio_bitstream_reader_aligned_eof(const zserio_bitstream_reader_t *bs,
                                         size_t alignment);

/* ------------------------------------------------------------------------ */
/* Writer                                                                   */
/* ------------------------------------------------------------------------ */

typedef struct zserio_bitstream_writer {
    uint8_t *buffer;                       /**< Borrowed; not owned. */
    size_t   buffer_capacity_bits;         /**< Total bits the buffer can hold. */
    size_t   bit_index;                    /**< Current write position in bits. */
} zserio_bitstream_writer_t;

/**
 * Initialize a writer over `buffer` with `buffer_capacity_bits` writable bits.
 * The byte range `[0, ceil(buffer_capacity_bits / 8))` is zeroed by this
 * call so partial-byte writes don't see garbage.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` or `buffer` is NULL
 * - `ZSERIO_ERR_INVALID_BUFFER` if `buffer_capacity_bits` is 0
 */
zserio_status_t zserio_bitstream_writer_init(zserio_bitstream_writer_t *bs,
                                             uint8_t *buffer,
                                             size_t buffer_capacity_bits);

/**
 * Write `num_bits` bits of an unsigned integer value.
 *
 * `num_bits` MUST be in [1, 64]. `value` MUST fit in `num_bits` bits;
 * if not, `ZSERIO_ERR_VALUE_OUT_OF_RANGE` is returned without modifying
 * the buffer.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` is NULL
 * - `ZSERIO_ERR_INVALID_NUM_BITS` if `num_bits` is 0 or > 64
 * - `ZSERIO_ERR_VALUE_OUT_OF_RANGE` if `value` does not fit in `num_bits`
 * - `ZSERIO_ERR_BUFFER_OVERFLOW` if not enough capacity remains
 */
zserio_status_t zserio_bitstream_write_bits(zserio_bitstream_writer_t *bs,
                                            uint8_t num_bits,
                                            uint64_t value);

/**
 * Write `num_bits` bits of a signed integer value (two's complement).
 *
 * Same constraints and error returns as `zserio_bitstream_write_bits`.
 * Range check uses [-(2^(N-1)), 2^(N-1) - 1].
 */
zserio_status_t zserio_bitstream_write_signed(zserio_bitstream_writer_t *bs,
                                              uint8_t num_bits,
                                              int64_t value);

/**
 * Advance the write position to the next multiple of `alignment` bits,
 * writing zero bits as padding.
 *
 * Same constraints as `zserio_bitstream_reader_align`. On capacity
 * exhaustion returns `ZSERIO_ERR_BUFFER_OVERFLOW`.
 */
zserio_status_t zserio_bitstream_writer_align(zserio_bitstream_writer_t *bs,
                                              size_t alignment);

/**
 * Returns the current write position in bits, or 0 if `bs` is NULL.
 */
size_t zserio_bitstream_writer_position(const zserio_bitstream_writer_t *bs);

/**
 * Set the write position to `bit_position`. Used for backward seek
 * during offset patching (write a placeholder, write the dependent
 * field, seek back, overwrite the placeholder, seek forward).
 *
 * Any position in [0, capacity] is allowed. Bits between `bit_index`
 * and the maximum position previously reached are NOT cleared by
 * seeking; the buffer was zeroed at init time, but if the caller
 * overwrites a region and then seeks back over it, the previously-
 * written bits remain. The caller is responsible for tracking what
 * has been written.
 *
 * Returns:
 * - `ZSERIO_OK` on success
 * - `ZSERIO_ERR_INVALID_ARGUMENT` if `bs` is NULL
 * - `ZSERIO_ERR_INVALID_POSITION` if `bit_position` exceeds capacity
 */
zserio_status_t zserio_bitstream_writer_seek(zserio_bitstream_writer_t *bs,
                                             size_t bit_position);

/* -------- diagnostic: per-primitive-read trace --------
 * Hook for the cpp-oracle wire-format diff (see
 * conformance/ndslive/cpp/read_tile_cpp.cpp). Enabled at runtime when
 * `ZSERIO_TRACE=<path>` is set in the environment AND
 * `zsc_trace_set_enabled(true)` has been called. Each call to
 * `zserio_bitstream_read_bits` (which all primitive reads ultimately
 * funnel through) emits one line to <path> in the form
 *   <pos_after> <op> <nbits> <value>
 * with the same format the cpp oracle emits. The two traces can be
 * diff'd to localise wire-format divergence to a specific bit
 * position. NOT part of the public ABI — the symbol is exposed so
 * a test driver can scope the trace to a single layer's decode. */
void zsc_trace_set_enabled(bool enabled);

#endif /* ZSERIO_BITSTREAM_H_ */
