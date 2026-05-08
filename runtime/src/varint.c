/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/varint.h"

#include <string.h>

/*
 * Generic variable-length integer encoder and decoder.
 *
 * One pair of helpers handles every type: read_varuint_impl /
 * write_varuint_impl for the unsigned family and read_varint_impl /
 * write_varint_impl for the signed family. Per-type wrappers at the
 * bottom of this file pass the right max-bytes / max-value table.
 *
 * The encoding spec is in docs/upstream-analysis/02-wire-format.md §2.3
 * and the per-type bit budgets are summarised in varint.h.
 */

/* ------------------------------------------------------------------------ */
/* Per-type max-magnitude tables (one entry per byte count, entries are the
 * largest magnitude representable when using exactly that many bytes).     */
/* ------------------------------------------------------------------------ */

#define MASK_7  ((uint64_t)0x7FU)
#define MASK_6  ((uint64_t)0x3FU)
#define HAS_NEXT_7 ((uint64_t)0x80U)
#define HAS_NEXT_6 ((uint64_t)0x40U)
#define SIGN_BIT   ((uint64_t)0x80U)

/* Unsigned tables: max value when encoded in (i+1) bytes. */
static const uint64_t VARUINT16_MAX[2] = {
    ((uint64_t)1U << 7U)  - 1U,    /* 1 byte:  127 */
    ((uint64_t)1U << 15U) - 1U     /* 2 bytes: 32767 */
};
static const uint64_t VARUINT32_MAX[4] = {
    ((uint64_t)1U << 7U)  - 1U,
    ((uint64_t)1U << 14U) - 1U,
    ((uint64_t)1U << 21U) - 1U,
    ((uint64_t)1U << 29U) - 1U     /* 4 bytes: 2^29 - 1 */
};
static const uint64_t VARUINT64_MAX[8] = {
    ((uint64_t)1U << 7U)  - 1U,
    ((uint64_t)1U << 14U) - 1U,
    ((uint64_t)1U << 21U) - 1U,
    ((uint64_t)1U << 28U) - 1U,
    ((uint64_t)1U << 35U) - 1U,
    ((uint64_t)1U << 42U) - 1U,
    ((uint64_t)1U << 49U) - 1U,
    ((uint64_t)1U << 57U) - 1U     /* 8 bytes: 2^57 - 1 */
};
static const uint64_t VARUINT_MAX[9] = {
    ((uint64_t)1U << 7U)  - 1U,
    ((uint64_t)1U << 14U) - 1U,
    ((uint64_t)1U << 21U) - 1U,
    ((uint64_t)1U << 28U) - 1U,
    ((uint64_t)1U << 35U) - 1U,
    ((uint64_t)1U << 42U) - 1U,
    ((uint64_t)1U << 49U) - 1U,
    ((uint64_t)1U << 56U) - 1U,
    UINT64_MAX                     /* 9 bytes: any uint64 */
};
static const uint64_t VARSIZE_MAX[5] = {
    ((uint64_t)1U << 7U)  - 1U,
    ((uint64_t)1U << 14U) - 1U,
    ((uint64_t)1U << 21U) - 1U,
    ((uint64_t)1U << 28U) - 1U,
    ((uint64_t)1U << 31U) - 1U     /* 5 bytes: 2^31 - 1 (artificial cap) */
};

/* Signed magnitude tables. The first byte carries 6 magnitude bits
 * (one fewer than unsigned because of the sign bit). */
static const uint64_t VARINT16_MAX[2] = {
    ((uint64_t)1U << 6U)  - 1U,
    ((uint64_t)1U << 14U) - 1U     /* 2 bytes: 2^14 - 1 = 16383 */
};
static const uint64_t VARINT32_MAX[4] = {
    ((uint64_t)1U << 6U)  - 1U,
    ((uint64_t)1U << 13U) - 1U,
    ((uint64_t)1U << 20U) - 1U,
    ((uint64_t)1U << 28U) - 1U
};
static const uint64_t VARINT64_MAX[8] = {
    ((uint64_t)1U << 6U)  - 1U,
    ((uint64_t)1U << 13U) - 1U,
    ((uint64_t)1U << 20U) - 1U,
    ((uint64_t)1U << 27U) - 1U,
    ((uint64_t)1U << 34U) - 1U,
    ((uint64_t)1U << 41U) - 1U,
    ((uint64_t)1U << 48U) - 1U,
    ((uint64_t)1U << 56U) - 1U
};
static const uint64_t VARINT_MAX[9] = {
    ((uint64_t)1U << 6U)  - 1U,
    ((uint64_t)1U << 13U) - 1U,
    ((uint64_t)1U << 20U) - 1U,
    ((uint64_t)1U << 27U) - 1U,
    ((uint64_t)1U << 34U) - 1U,
    ((uint64_t)1U << 41U) - 1U,
    ((uint64_t)1U << 48U) - 1U,
    ((uint64_t)1U << 55U) - 1U,
    ((uint64_t)1U << 63U) - 1U     /* 9 bytes: 2^63 - 1 = INT64_MAX */
};

/* ------------------------------------------------------------------------ */
/* Generic unsigned read                                                    */
/* ------------------------------------------------------------------------ */

static zserio_status_t read_varuint_impl(zserio_bitstream_reader_t *bs,
                                         uint8_t max_bytes,
                                         uint64_t cap_value,   /* 0 means no extra cap */
                                         uint64_t *out)
{
    uint64_t result = 0U;
    for (uint8_t i = 0U; i < max_bytes; i++) {
        uint64_t byte = 0U;
        ZSERIO_TRY(zserio_bitstream_read_bits(bs, 8U, &byte));

        const bool is_last = (i == (uint8_t)(max_bytes - 1U));
        if (is_last) {
            /* Last byte: full 8 bits of value, no continuation bit. */
            if (i == 0U) {
                /* Single-byte type? Only varuint16 with max_bytes=2 doesn't
                 * hit this branch — the smallest unsigned varint is 2 bytes
                 * max. We never call read_varuint_impl with max_bytes==1. */
                result = byte;
            } else {
                result = (result << 8U) | byte;
            }
            if ((cap_value != 0U) && (result > cap_value)) {
                return ZSERIO_ERR_INVALID_ENCODING;
            }
            *out = result;
            return ZSERIO_OK;
        }

        const bool has_next = (byte & HAS_NEXT_7) != 0U;
        const uint64_t value_bits = byte & MASK_7;
        if (i == 0U) {
            result = value_bits;
        } else {
            result = (result << 7U) | value_bits;
        }
        if (!has_next) {
            *out = result;
            return ZSERIO_OK;
        }
    }
    /* Unreachable: the loop returns from the is_last branch on the
     * final iteration. Defensive fall-through for the static analyser. */
    return ZSERIO_ERR_INVALID_ENCODING;
}

/* ------------------------------------------------------------------------ */
/* Generic signed read                                                      */
/* ------------------------------------------------------------------------ */

static zserio_status_t read_varint_impl(zserio_bitstream_reader_t *bs,
                                        uint8_t max_bytes,
                                        bool support_int64_min,
                                        int64_t *out)
{
    uint64_t byte = 0U;
    ZSERIO_TRY(zserio_bitstream_read_bits(bs, 8U, &byte));

    const bool sign = (byte & SIGN_BIT) != 0U;
    const bool has_next = (byte & HAS_NEXT_6) != 0U;
    uint64_t magnitude = byte & MASK_6;

    if (!has_next) {
        if (sign) {
            if ((magnitude == 0U) && support_int64_min) {
                *out = INT64_MIN;
            } else {
                /* Negate via signed arithmetic. magnitude fits in 6 bits
                 * here so it cannot exceed INT64_MAX. */
                *out = -(int64_t)magnitude;
            }
        } else {
            *out = (int64_t)magnitude;
        }
        return ZSERIO_OK;
    }

    /* Continue reading bytes 1..(max_bytes - 1). */
    for (uint8_t i = 1U; i < max_bytes; i++) {
        ZSERIO_TRY(zserio_bitstream_read_bits(bs, 8U, &byte));

        const bool is_last = (i == (uint8_t)(max_bytes - 1U));
        if (is_last) {
            magnitude = (magnitude << 8U) | byte;
            *out = sign ? -(int64_t)magnitude : (int64_t)magnitude;
            return ZSERIO_OK;
        }

        const bool n_has_next = (byte & HAS_NEXT_7) != 0U;
        const uint64_t value_bits = byte & MASK_7;
        magnitude = (magnitude << 7U) | value_bits;
        if (!n_has_next) {
            *out = sign ? -(int64_t)magnitude : (int64_t)magnitude;
            return ZSERIO_OK;
        }
    }
    return ZSERIO_ERR_INVALID_ENCODING;
}

/* ------------------------------------------------------------------------ */
/* Generic unsigned write                                                   */
/* ------------------------------------------------------------------------ */

static zserio_status_t write_varuint_impl(zserio_bitstream_writer_t *bs,
                                          uint64_t value,
                                          const uint64_t *max_table,
                                          uint8_t max_bytes)
{
    /* Find the smallest byte count that can hold this value. */
    uint8_t bytes_needed = 0U;
    for (uint8_t i = 0U; i < max_bytes; i++) {
        if (value <= max_table[i]) {
            bytes_needed = (uint8_t)(i + 1U);
            break;
        }
    }
    if (bytes_needed == 0U) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }

    /* Total magnitude bits: 7 per intermediate byte plus 8 for the
     * last byte if at max length, or 7 for the last byte otherwise. */
    const uint8_t last_byte_bits = (bytes_needed == max_bytes) ? 8U : 7U;
    const uint8_t total_value_bits = (uint8_t)(7U * (bytes_needed - 1U) + last_byte_bits);

    uint8_t bits_used = 0U;
    for (uint8_t i = 0U; i < bytes_needed; i++) {
        const bool is_last = (i == (uint8_t)(bytes_needed - 1U));
        const uint8_t byte_bits = is_last ? last_byte_bits : 7U;
        const uint8_t shift = (uint8_t)(total_value_bits - bits_used - byte_bits);
        const uint64_t mask = (byte_bits == 8U)
                              ? (uint64_t)0xFFU
                              : (((uint64_t)1U << byte_bits) - 1U);
        const uint64_t byte_value = (value >> shift) & mask;
        bits_used = (uint8_t)(bits_used + byte_bits);

        uint64_t out_byte;
        if (is_last && (bytes_needed == max_bytes)) {
            /* Last byte at max length: 8 value bits, no cont bit. */
            out_byte = byte_value;
        } else {
            const uint64_t cont = is_last ? (uint64_t)0U : HAS_NEXT_7;
            out_byte = cont | byte_value;
        }
        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 8U, out_byte));
    }
    return ZSERIO_OK;
}

/* ------------------------------------------------------------------------ */
/* Generic signed write                                                     */
/* ------------------------------------------------------------------------ */

static zserio_status_t write_varint_impl(zserio_bitstream_writer_t *bs,
                                         int64_t value,
                                         const uint64_t *max_table,
                                         uint8_t max_bytes,
                                         bool support_int64_min)
{
    /* INT64_MIN special case (varint only). */
    if ((value == INT64_MIN) && support_int64_min) {
        /* sign=1, cont=0, magnitude=0 → byte = 0x80. */
        return zserio_bitstream_write_bits(bs, 8U, SIGN_BIT);
    }

    const bool sign = (value < 0);
    /* Magnitude. We must avoid -INT64_MIN (= INT64_MAX + 1, UB on overflow). */
    uint64_t magnitude;
    if (sign) {
        if (value == INT64_MIN) {
            /* Out of range when not handled above (varint16/32/64). */
            return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
        }
        magnitude = (uint64_t)(-value);
    } else {
        magnitude = (uint64_t)value;
    }

    /* Find the smallest byte count that fits this magnitude. */
    uint8_t bytes_needed = 0U;
    for (uint8_t i = 0U; i < max_bytes; i++) {
        if (magnitude <= max_table[i]) {
            bytes_needed = (uint8_t)(i + 1U);
            break;
        }
    }
    if (bytes_needed == 0U) {
        return ZSERIO_ERR_VALUE_OUT_OF_RANGE;
    }

    const uint8_t last_byte_bits = (bytes_needed == max_bytes) ? 8U : 7U;
    /* Total magnitude bits: 6 (byte 0) + 7 each for intermediate bytes
     * + last_byte_bits. When bytes_needed == 1, total is just 6. */
    uint8_t total_value_bits;
    if (bytes_needed == 1U) {
        total_value_bits = 6U;
    } else {
        total_value_bits = (uint8_t)(6U + (7U * (bytes_needed - 2U)) + last_byte_bits);
    }

    uint8_t bits_used = 0U;
    for (uint8_t i = 0U; i < bytes_needed; i++) {
        const bool is_first = (i == 0U);
        const bool is_last  = (i == (uint8_t)(bytes_needed - 1U));
        const uint8_t byte_bits = is_first ? 6U : (is_last ? last_byte_bits : 7U);
        const uint8_t shift = (uint8_t)(total_value_bits - bits_used - byte_bits);
        const uint64_t mask = (byte_bits == 8U)
                              ? (uint64_t)0xFFU
                              : (((uint64_t)1U << byte_bits) - 1U);
        const uint64_t byte_value = (magnitude >> shift) & mask;
        bits_used = (uint8_t)(bits_used + byte_bits);

        uint64_t out_byte;
        if (is_first) {
            /* sign + cont + 6 value bits */
            const uint64_t sign_bits = sign ? SIGN_BIT : (uint64_t)0U;
            const uint64_t cont_bits = is_last ? (uint64_t)0U : HAS_NEXT_6;
            out_byte = sign_bits | cont_bits | byte_value;
        } else if (is_last && (bytes_needed == max_bytes)) {
            /* Last byte at max length: 8 value bits, no cont. */
            out_byte = byte_value;
        } else {
            const uint64_t cont_bits = is_last ? (uint64_t)0U : HAS_NEXT_7;
            out_byte = cont_bits | byte_value;
        }
        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 8U, out_byte));
    }
    return ZSERIO_OK;
}

/* ------------------------------------------------------------------------ */
/* Per-type read wrappers                                                   */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_read_varuint16(zserio_bitstream_reader_t *bs, uint16_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(read_varuint_impl(bs, 2U, 0U, &raw));
    *out = (uint16_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_varuint32(zserio_bitstream_reader_t *bs, uint32_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(read_varuint_impl(bs, 4U, 0U, &raw));
    *out = (uint32_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_varuint64(zserio_bitstream_reader_t *bs, uint64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return read_varuint_impl(bs, 8U, 0U, out);
}

zserio_status_t zserio_read_varuint(zserio_bitstream_reader_t *bs, uint64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return read_varuint_impl(bs, 9U, 0U, out);
}

zserio_status_t zserio_read_varsize(zserio_bitstream_reader_t *bs, uint32_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    uint64_t raw = 0U;
    ZSERIO_TRY(read_varuint_impl(bs, 5U, ((uint64_t)1U << 31U) - 1U, &raw));
    *out = (uint32_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_varint16(zserio_bitstream_reader_t *bs, int16_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    int64_t raw = 0;
    ZSERIO_TRY(read_varint_impl(bs, 2U, false, &raw));
    *out = (int16_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_varint32(zserio_bitstream_reader_t *bs, int32_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    int64_t raw = 0;
    ZSERIO_TRY(read_varint_impl(bs, 4U, false, &raw));
    *out = (int32_t)raw;
    return ZSERIO_OK;
}

zserio_status_t zserio_read_varint64(zserio_bitstream_reader_t *bs, int64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return read_varint_impl(bs, 8U, false, out);
}

zserio_status_t zserio_read_varint(zserio_bitstream_reader_t *bs, int64_t *out)
{
    if (out == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    return read_varint_impl(bs, 9U, true, out);
}

/* ------------------------------------------------------------------------ */
/* Per-type write wrappers                                                  */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_varuint16(zserio_bitstream_writer_t *bs, uint16_t v)
{
    return write_varuint_impl(bs, (uint64_t)v, VARUINT16_MAX, 2U);
}

zserio_status_t zserio_write_varuint32(zserio_bitstream_writer_t *bs, uint32_t v)
{
    return write_varuint_impl(bs, (uint64_t)v, VARUINT32_MAX, 4U);
}

zserio_status_t zserio_write_varuint64(zserio_bitstream_writer_t *bs, uint64_t v)
{
    return write_varuint_impl(bs, v, VARUINT64_MAX, 8U);
}

zserio_status_t zserio_write_varuint(zserio_bitstream_writer_t *bs, uint64_t v)
{
    return write_varuint_impl(bs, v, VARUINT_MAX, 9U);
}

zserio_status_t zserio_write_varsize(zserio_bitstream_writer_t *bs, uint32_t v)
{
    return write_varuint_impl(bs, (uint64_t)v, VARSIZE_MAX, 5U);
}

zserio_status_t zserio_write_varint16(zserio_bitstream_writer_t *bs, int16_t v)
{
    return write_varint_impl(bs, (int64_t)v, VARINT16_MAX, 2U, false);
}

zserio_status_t zserio_write_varint32(zserio_bitstream_writer_t *bs, int32_t v)
{
    return write_varint_impl(bs, (int64_t)v, VARINT32_MAX, 4U, false);
}

zserio_status_t zserio_write_varint64(zserio_bitstream_writer_t *bs, int64_t v)
{
    return write_varint_impl(bs, v, VARINT64_MAX, 8U, false);
}

zserio_status_t zserio_write_varint(zserio_bitstream_writer_t *bs, int64_t v)
{
    return write_varint_impl(bs, v, VARINT_MAX, 9U, true);
}
