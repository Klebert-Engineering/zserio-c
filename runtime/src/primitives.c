/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/primitives.h"

/*
 * Implementation strategy: each function is a thin wrapper that
 * narrows the width parameter and the output type. The underlying
 * `bitstream` calls do the heavy lifting (boundary validation,
 * end-of-stream / overflow checks, range validation for signed types).
 *
 * The output-pointer NULL check lives here, in the caller of
 * `bitstream.read_bits` / `read_signed`, because those functions also
 * accept NULL output pointers as `ZSERIO_ERR_INVALID_ARGUMENT` — but
 * we want the error attributed to the primitive's `out` parameter.
 * Doing the check up-front keeps that contract honest.
 */

/* ------------------------------------------------------------------------ */
/* Unsigned reads                                                           */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_uint8(zserio_bitstream_reader_t *bs, uint8_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(zserio_bitstream_read_bits(bs, 8U, &raw));
    *out = (uint8_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_uint16(zserio_bitstream_reader_t *bs, uint16_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(zserio_bitstream_read_bits(bs, 16U, &raw));
    *out = (uint16_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_uint32(zserio_bitstream_reader_t *bs, uint32_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(zserio_bitstream_read_bits(bs, 32U, &raw));
    *out = (uint32_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_uint64(zserio_bitstream_reader_t *bs, uint64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return zserio_bitstream_read_bits(bs, 64U, out);
}

/* ------------------------------------------------------------------------ */
/* Signed reads                                                             */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_int8(zserio_bitstream_reader_t *bs, int8_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    int64_t raw = 0;
    ZSERIO_TRY(zserio_bitstream_read_signed(bs, 8U, &raw));
    *out = (int8_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_int16(zserio_bitstream_reader_t *bs, int16_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    int64_t raw = 0;
    ZSERIO_TRY(zserio_bitstream_read_signed(bs, 16U, &raw));
    *out = (int16_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_int32(zserio_bitstream_reader_t *bs, int32_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    int64_t raw = 0;
    ZSERIO_TRY(zserio_bitstream_read_signed(bs, 32U, &raw));
    *out = (int32_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_int64(zserio_bitstream_reader_t *bs, int64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return zserio_bitstream_read_signed(bs, 64U, out);
}

/* ------------------------------------------------------------------------ */
/* Bool                                                                     */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_bool(zserio_bitstream_reader_t *bs, bool *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1U, &raw));
    *out = (raw != 0U);
    return ZSERIO_OK;
}

/* ------------------------------------------------------------------------ */
/* Unsigned writes                                                          */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_uint8(zserio_bitstream_writer_t *bs, uint8_t v)
{
    return zserio_bitstream_write_bits(bs, 8U, (uint64_t)v);
}

zserio_status_t zserio_write_uint16(zserio_bitstream_writer_t *bs, uint16_t v)
{
    return zserio_bitstream_write_bits(bs, 16U, (uint64_t)v);
}

zserio_status_t zserio_write_uint32(zserio_bitstream_writer_t *bs, uint32_t v)
{
    return zserio_bitstream_write_bits(bs, 32U, (uint64_t)v);
}

zserio_status_t zserio_write_uint64(zserio_bitstream_writer_t *bs, uint64_t v)
{
    return zserio_bitstream_write_bits(bs, 64U, v);
}

/* ------------------------------------------------------------------------ */
/* Signed writes                                                            */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_int8(zserio_bitstream_writer_t *bs, int8_t v)
{
    return zserio_bitstream_write_signed(bs, 8U, (int64_t)v);
}

zserio_status_t zserio_write_int16(zserio_bitstream_writer_t *bs, int16_t v)
{
    return zserio_bitstream_write_signed(bs, 16U, (int64_t)v);
}

zserio_status_t zserio_write_int32(zserio_bitstream_writer_t *bs, int32_t v)
{
    return zserio_bitstream_write_signed(bs, 32U, (int64_t)v);
}

zserio_status_t zserio_write_int64(zserio_bitstream_writer_t *bs, int64_t v)
{
    return zserio_bitstream_write_signed(bs, 64U, v);
}

/* ------------------------------------------------------------------------ */
/* Bool                                                                     */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_bool(zserio_bitstream_writer_t *bs, bool v)
{
    return zserio_bitstream_write_bits(bs, 1U, v ? (uint64_t)1U : (uint64_t)0U);
}
