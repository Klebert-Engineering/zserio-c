/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_BITSTREAM_H_
#define ZSERIO_BITSTREAM_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

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
    size_t         bit_index;         /**< Current read position in bits. */
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

#endif /* ZSERIO_BITSTREAM_H_ */
