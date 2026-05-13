/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_PRIMITIVES_H_
#define ZSERIO_PRIMITIVES_H_

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "zserio/bitstream.h"
#include "zserio/status.h"

/**
 * @file
 * Read and write helpers for the fixed-width built-in zserio types
 * (`uint8`/`uint16`/`uint32`/`uint64`, `int8`/`int16`/`int32`/`int64`, `bool`).
 *
 * Each helper is a typed wrapper over `zserio_bitstream_read_bits` /
 * `read_signed` (and their writer counterparts) with a `(num_bits)` of
 * 8/16/32/64 (or 1 for `bool`). The wrappers are here so generated
 * code stays self-documenting — `zserio_read_uint16(&bs, &v)` reads
 * better than `zserio_bitstream_read_bits(&bs, 16, &raw)` + cast.
 *
 * Bit-field accessors of variable width (`bit:N`, `int:N`,
 * `bit<expr>`, `int<expr>`) are exposed by `bitstream.h` directly
 * as `zserio_bitstream_read_bits` / `read_signed` / `write_bits` /
 * `write_signed`. There is no separate "bits.h" — that would be pure
 * aliases.
 *
 * All functions return `ZSERIO_ERR_INVALID_ARGUMENT` when `out` (read
 * functions) is NULL. NULL `bs` is detected by the underlying
 * `bitstream` call and surfaced unchanged. Other errors propagate
 * from the underlying call (`ZSERIO_ERR_END_OF_STREAM`,
 * `ZSERIO_ERR_BUFFER_OVERFLOW`).
 */

/* ------------------------------------------------------------------------ */
/* Reads                                                                    */
/*                                                                          */
/* These are `static inline` so each caller's translation unit gets a       */
/* full inlining of the cache fast path — no TU-crossing call for the     */
/* common case. The shared validation pattern is:                           */
/*   1. NULL out → INVALID_ARGUMENT.                                        */
/*   2. NULL bs or bit-index overflow → fall through to                     */
/*      `zserio_bitstream_read_bits` which surfaces the correct error.     */
/*   3. End-of-stream → INVALID error code from the impl.                  */
/* The hot path on a valid call is ~5 instructions.                         */
/* ------------------------------------------------------------------------ */

/* Unsigned reads. The bodies are hand-expanded rather than emitted
 * by a type-parametric macro so the file stays free of the Rule
 * 20.7 / 20.10 idiom that needs a deviation entry (the runtime-
 * traits dispatcher in varint.c already burns one such deviation;
 * we don't multiply). */

static inline zserio_status_t zserio_read_uint8(zserio_bitstream_reader_t *bs, uint8_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 8U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = (uint8_t)zserio_bitstream_read_bits_impl(bs, 8U);
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_uint16(zserio_bitstream_reader_t *bs, uint16_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 16U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = (uint16_t)zserio_bitstream_read_bits_impl(bs, 16U);
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_uint32(zserio_bitstream_reader_t *bs, uint32_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 32U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = (uint32_t)zserio_bitstream_read_bits_impl(bs, 32U);
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_uint64(zserio_bitstream_reader_t *bs, uint64_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 64U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    *out = zserio_bitstream_read_bits_impl(bs, 64U);
    return ZSERIO_OK;
}

/* Signed reads: extract, sign-extend, narrow.
 *
 * For N == 64 the sign-extension is a no-op (the value already
 * occupies all bits), so int64 has its own body without the
 * sign-bit branch — saves the branch entirely on the 64-bit path. */

static inline int64_t zserio_primitives_sign_extend(uint64_t raw, uint8_t num_bits)
{
    const uint64_t sign_bit = (uint64_t)1U << (uint64_t)(num_bits - 1U);
    uint64_t v = raw;
    if ((v & sign_bit) != 0U) {
        v |= ~zserio_bitstream_mask_table[num_bits];
    }
    int64_t signed_value;
    (void)memcpy(&signed_value, &v, sizeof(signed_value));
    return signed_value;
}

static inline zserio_status_t zserio_read_int8(zserio_bitstream_reader_t *bs, int8_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 8U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    const uint64_t raw = zserio_bitstream_read_bits_impl(bs, 8U);
    *out = (int8_t)zserio_primitives_sign_extend(raw, 8U);
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_int16(zserio_bitstream_reader_t *bs, int16_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 16U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    const uint64_t raw = zserio_bitstream_read_bits_impl(bs, 16U);
    *out = (int16_t)zserio_primitives_sign_extend(raw, 16U);
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_int32(zserio_bitstream_reader_t *bs, int32_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 32U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    const uint64_t raw = zserio_bitstream_read_bits_impl(bs, 32U);
    *out = (int32_t)zserio_primitives_sign_extend(raw, 32U);
    return ZSERIO_OK;
}

static inline zserio_status_t zserio_read_int64(zserio_bitstream_reader_t *bs, int64_t *out)
{
    if (out == NULL) { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if (bs == NULL)  { return ZSERIO_ERR_INVALID_ARGUMENT; }
    if ((bs->bit_index + 64U) > bs->buffer_bit_size) { return ZSERIO_ERR_END_OF_STREAM; }
    const uint64_t raw = zserio_bitstream_read_bits_impl(bs, 64U);
    int64_t signed_value;
    (void)memcpy(&signed_value, &raw, sizeof(signed_value));
    *out = signed_value;
    return ZSERIO_OK;
}

/**
 * Read a single bit as a boolean. `1` becomes `true`, `0` becomes `false`.
 */
static inline zserio_status_t zserio_read_bool(zserio_bitstream_reader_t *bs, bool *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if (bs == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((bs->bit_index + 1U) > bs->buffer_bit_size) {
        return ZSERIO_ERR_END_OF_STREAM;
    }
    *out = (zserio_bitstream_read_bits_impl(bs, 1U) != 0U);
    return ZSERIO_OK;
}

/* ------------------------------------------------------------------------ */
/* Writes                                                                   */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_uint8 (zserio_bitstream_writer_t *bs, uint8_t  v);
zserio_status_t zserio_write_uint16(zserio_bitstream_writer_t *bs, uint16_t v);
zserio_status_t zserio_write_uint32(zserio_bitstream_writer_t *bs, uint32_t v);
zserio_status_t zserio_write_uint64(zserio_bitstream_writer_t *bs, uint64_t v);

zserio_status_t zserio_write_int8 (zserio_bitstream_writer_t *bs, int8_t  v);
zserio_status_t zserio_write_int16(zserio_bitstream_writer_t *bs, int16_t v);
zserio_status_t zserio_write_int32(zserio_bitstream_writer_t *bs, int32_t v);
zserio_status_t zserio_write_int64(zserio_bitstream_writer_t *bs, int64_t v);

/**
 * Write a boolean as a single bit. `false` → `0`, `true` → `1`.
 */
zserio_status_t zserio_write_bool(zserio_bitstream_writer_t *bs, bool v);

#endif /* ZSERIO_PRIMITIVES_H_ */
