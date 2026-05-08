/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/delta.h"

#include <string.h>

/*
 * Algorithm ported from upstream DeltaContext.h at the pinned tag.
 * Fixed-width integer subset: variable-length (varint*) packed
 * elements are not supported by this module.
 *
 * Three flag bits track the lifecycle:
 *   INIT_STARTED: at least one observe() call has been made
 *   IS_PACKED:    finalised decision (or running guess during init)
 *   USE_STARTED:  first write() / read() has happened
 *   FINALISED:    finalize() has run; IS_PACKED is the final answer
 *
 * MAX_BIT_NUMBER_LIMIT matches upstream: 62. Above this, packed
 * encoding cannot represent the delta and we fall back to unpacked.
 * (At maxBitNumber = 62, the delta storage is int<63> with range
 *  [-2^62, 2^62-1].)
 */

#define DELTA_FLAG_INIT_STARTED  ((uint8_t)0x01U)
#define DELTA_FLAG_IS_PACKED     ((uint8_t)0x02U)
#define DELTA_FLAG_USE_STARTED   ((uint8_t)0x04U)
#define DELTA_FLAG_FINALISED     ((uint8_t)0x08U)

#define DELTA_MAX_BIT_NUMBER_LIMIT  ((uint8_t)62U)
#define DELTA_MAX_BIT_NUMBER_BITS   ((uint8_t)6U)

static uint8_t abs_delta_bit_length(uint64_t abs_delta)
{
    uint8_t result = 0U;
    while (abs_delta > 0U) {
        result++;
        abs_delta >>= 1U;
    }
    return result;
}

static uint64_t abs_delta(uint64_t a, uint64_t b, bool is_signed)
{
    /* Wrap-arithmetic trick: regardless of signedness, the absolute
     * difference is the larger-minus-smaller in uint64 land, where
     * the signed-aware comparison decides which is larger. */
    bool a_greater = is_signed ? ((int64_t)a > (int64_t)b) : (a > b);
    return a_greater ? (a - b) : (b - a);
}

static void delta_finalise(zserio_delta_context_t *ctx)
{
    if ((ctx->flags & DELTA_FLAG_FINALISED) != 0U) {
        return;
    }
    ctx->flags |= DELTA_FLAG_FINALISED;

    if ((ctx->flags & DELTA_FLAG_IS_PACKED) == 0U) {
        return; /* already disqualified during init */
    }
    if (ctx->num_elements <= 1U) {
        /* Single element: descriptor adds overhead with no delta savings. */
        ctx->flags &= (uint8_t)~DELTA_FLAG_IS_PACKED;
        return;
    }
    /* Compare packed vs. unpacked total bit costs (excluding the
     * 1-bit isPacked flag, which is paid in either case). */
    const uint64_t packed_payload =
        (uint64_t)DELTA_MAX_BIT_NUMBER_BITS
        + (uint64_t)ctx->num_bits
        + (uint64_t)(ctx->num_elements - 1U) * ((uint64_t)ctx->max_bit_number + 1U);
    if (packed_payload >= ctx->total_unpacked_bits) {
        ctx->flags &= (uint8_t)~DELTA_FLAG_IS_PACKED;
    }
}

static zserio_status_t write_full_element(const zserio_delta_context_t *ctx,
                                          zserio_bitstream_writer_t *bs,
                                          uint64_t element)
{
    if (ctx->is_signed) {
        return zserio_bitstream_write_signed(bs, ctx->num_bits, (int64_t)element);
    }
    return zserio_bitstream_write_bits(bs, ctx->num_bits, element);
}

static zserio_status_t read_full_element(const zserio_delta_context_t *ctx,
                                         zserio_bitstream_reader_t *bs,
                                         uint64_t *out)
{
    if (ctx->is_signed) {
        int64_t v = 0;
        ZSERIO_TRY(zserio_bitstream_read_signed(bs, ctx->num_bits, &v));
        *out = (uint64_t)v;
        return ZSERIO_OK;
    }
    return zserio_bitstream_read_bits(bs, ctx->num_bits, out);
}

zserio_status_t zserio_delta_init(zserio_delta_context_t *ctx,
                                  uint8_t num_bits,
                                  bool is_signed)
{
    if (ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((num_bits == 0U) || (num_bits > 64U)) {
        return ZSERIO_ERR_INVALID_NUM_BITS;
    }
    (void)memset(ctx, 0, sizeof(*ctx));
    ctx->num_bits = num_bits;
    ctx->is_signed = is_signed;
    return ZSERIO_OK;
}

zserio_status_t zserio_delta_observe(zserio_delta_context_t *ctx,
                                     uint64_t element)
{
    if (ctx == NULL) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    if ((ctx->flags & DELTA_FLAG_FINALISED) != 0U) {
        /* Once finalised, no more observations. */
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    ctx->num_elements++;
    ctx->total_unpacked_bits += ctx->num_bits;

    if ((ctx->flags & DELTA_FLAG_INIT_STARTED) == 0U) {
        ctx->flags |= DELTA_FLAG_INIT_STARTED;
        ctx->flags |= DELTA_FLAG_IS_PACKED; /* tentative; finalise may revoke */
        ctx->previous = element;
        return ZSERIO_OK;
    }

    if (ctx->max_bit_number <= DELTA_MAX_BIT_NUMBER_LIMIT) {
        const uint64_t ad = abs_delta(element, ctx->previous, ctx->is_signed);
        const uint8_t bits = abs_delta_bit_length(ad);
        if (bits > ctx->max_bit_number) {
            ctx->max_bit_number = bits;
            if (ctx->max_bit_number > DELTA_MAX_BIT_NUMBER_LIMIT) {
                ctx->flags &= (uint8_t)~DELTA_FLAG_IS_PACKED;
            }
        }
    }
    ctx->previous = element;
    return ZSERIO_OK;
}

zserio_status_t zserio_delta_write(zserio_delta_context_t *ctx,
                                   zserio_bitstream_writer_t *bs,
                                   uint64_t element)
{
    if ((ctx == NULL) || (bs == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }
    delta_finalise(ctx);

    if ((ctx->flags & DELTA_FLAG_USE_STARTED) == 0U) {
        ctx->flags |= DELTA_FLAG_USE_STARTED;
        const bool packed = (ctx->flags & DELTA_FLAG_IS_PACKED) != 0U;
        ZSERIO_TRY(zserio_bitstream_write_bits(bs, 1U, packed ? 1U : 0U));
        if (packed) {
            ZSERIO_TRY(zserio_bitstream_write_bits(bs,
                DELTA_MAX_BIT_NUMBER_BITS, (uint64_t)ctx->max_bit_number));
        }
        ZSERIO_TRY(write_full_element(ctx, bs, element));
        ctx->previous = element;
        return ZSERIO_OK;
    }

    if ((ctx->flags & DELTA_FLAG_IS_PACKED) != 0U) {
        /* Wrap-arithmetic delta. Width is max_bit_number + 1 (one extra
         * for the sign bit). */
        const uint64_t delta_bits = element - ctx->previous;
        ZSERIO_TRY(zserio_bitstream_write_signed(bs,
            (uint8_t)(ctx->max_bit_number + 1U), (int64_t)delta_bits));
    } else {
        ZSERIO_TRY(write_full_element(ctx, bs, element));
    }
    ctx->previous = element;
    return ZSERIO_OK;
}

zserio_status_t zserio_delta_read(zserio_delta_context_t *ctx,
                                  zserio_bitstream_reader_t *bs,
                                  uint64_t *out)
{
    if ((ctx == NULL) || (bs == NULL) || (out == NULL)) {
        return ZSERIO_ERR_INVALID_ARGUMENT;
    }

    if ((ctx->flags & DELTA_FLAG_USE_STARTED) == 0U) {
        ctx->flags |= DELTA_FLAG_USE_STARTED;
        uint64_t is_packed = 0U;
        ZSERIO_TRY(zserio_bitstream_read_bits(bs, 1U, &is_packed));
        if (is_packed != 0U) {
            ctx->flags |= DELTA_FLAG_IS_PACKED;
            uint64_t mbn = 0U;
            ZSERIO_TRY(zserio_bitstream_read_bits(bs, DELTA_MAX_BIT_NUMBER_BITS, &mbn));
            ctx->max_bit_number = (uint8_t)mbn;
        } else {
            ctx->flags &= (uint8_t)~DELTA_FLAG_IS_PACKED;
        }
        uint64_t first = 0U;
        ZSERIO_TRY(read_full_element(ctx, bs, &first));
        ctx->previous = first;
        *out = first;
        return ZSERIO_OK;
    }

    if ((ctx->flags & DELTA_FLAG_IS_PACKED) != 0U) {
        int64_t delta = 0;
        ZSERIO_TRY(zserio_bitstream_read_signed(bs,
            (uint8_t)(ctx->max_bit_number + 1U), &delta));
        ctx->previous = ctx->previous + (uint64_t)delta;
    } else {
        uint64_t v = 0U;
        ZSERIO_TRY(read_full_element(ctx, bs, &v));
        ctx->previous = v;
    }
    *out = ctx->previous;
    return ZSERIO_OK;
}
