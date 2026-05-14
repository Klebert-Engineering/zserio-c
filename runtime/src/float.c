/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/float.h"

#include <string.h>

/*
 * IEEE 754 conversions. Algorithm ported from
 * compiler/extensions/cpp/runtime/src/zserio/FloatUtil.cpp at the
 * pinned upstream tag v2.18.1, adapted to C99.
 *
 * Per REQ-FS-004, all logic is on integer bit patterns. The host's
 * `float` and `double` appear only at the boundary, never as
 * operands of comparisons, branches, or loop conditions.
 */

#define FLOAT16_SIGN_MASK              ((uint16_t)0x8000U)
#define FLOAT16_EXPONENT_MASK          ((uint16_t)0x7C00U)
#define FLOAT16_SIGNIFICAND_MASK       ((uint16_t)0x03FFU)
#define FLOAT16_SIGN_BIT_POSITION      ((uint16_t)15U)
#define FLOAT16_EXPONENT_BIT_POSITION  ((uint16_t)10U)
#define FLOAT16_SIGNIFICAND_NUM_BITS   FLOAT16_EXPONENT_BIT_POSITION
#define FLOAT16_EXPONENT_INFINITY_NAN  ((uint16_t)0x001FU)
#define FLOAT16_EXPONENT_BIAS          ((uint16_t)15U)

#define FLOAT32_SIGN_MASK              ((uint32_t)0x80000000U)
#define FLOAT32_EXPONENT_MASK          ((uint32_t)0x7F800000U)
#define FLOAT32_SIGNIFICAND_MASK       ((uint32_t)0x007FFFFFU)
#define FLOAT32_SIGN_BIT_POSITION      ((uint32_t)31U)
#define FLOAT32_EXPONENT_BIT_POSITION  ((uint32_t)23U)
#define FLOAT32_SIGNIFICAND_NUM_BITS   FLOAT32_EXPONENT_BIT_POSITION
#define FLOAT32_EXPONENT_INFINITY_NAN  ((uint32_t)0x00FFU)
#define FLOAT32_EXPONENT_BIAS          ((uint32_t)127U)

/* ------------------------------------------------------------------------ */
/* float32 / float64 narrow-to-integer (just memcpy)                         */
/*                                                                           */
/* The widen-from-integer direction (`zserio_float_from_u32` /               */
/* `zserio_double_from_u64`) is `static inline` in float.h so the hot decode */
/* path inlines it; only the encode-side narrow direction stays here.        */
/* ------------------------------------------------------------------------ */

uint32_t zserio_float_to_u32(float v)
{
    uint32_t bits = 0U;
    (void)memcpy(&bits, &v, sizeof(bits));
    return bits;
}

uint64_t zserio_double_to_u64(double v)
{
    uint64_t bits = 0U;
    (void)memcpy(&bits, &v, sizeof(bits));
    return bits;
}

/* ------------------------------------------------------------------------ */
/* float16 -> float32                                                       */
/* ------------------------------------------------------------------------ */

float zserio_float_from_u16(uint16_t v)
{
    /* Decompose half precision (float16). */
    const uint16_t sign16_shifted = (uint16_t)(v & FLOAT16_SIGN_MASK);
    const uint16_t exponent16 = (uint16_t)((v & FLOAT16_EXPONENT_MASK) >> FLOAT16_EXPONENT_BIT_POSITION);
    const uint16_t significand16 = (uint16_t)(v & FLOAT16_SIGNIFICAND_MASK);

    /* Compute single-precision significand by left-shifting. */
    uint32_t significand32 = (uint32_t)significand16
            << (uint32_t)(FLOAT32_SIGNIFICAND_NUM_BITS - FLOAT16_SIGNIFICAND_NUM_BITS);

    /* Compute single-precision exponent. */
    uint32_t exponent32 = 0U;
    if (exponent16 == 0U) {
        if (significand32 != 0U) {
            /* Subnormal half → normalize. */
            exponent32 = 1U + FLOAT32_EXPONENT_BIAS - FLOAT16_EXPONENT_BIAS;
            while ((significand32 & (FLOAT32_SIGNIFICAND_MASK + 1U)) == 0U) {
                exponent32--;
                significand32 <<= 1U;
            }
            /* Mask out the leading 1-bit (it becomes implicit). */
            significand32 &= FLOAT32_SIGNIFICAND_MASK;
        }
        /* Else: zero (sign-preserving). exponent32 stays 0. */
    } else if (exponent16 == FLOAT16_EXPONENT_INFINITY_NAN) {
        /* Infinity or NaN. */
        exponent32 = FLOAT32_EXPONENT_INFINITY_NAN;
    } else {
        /* Normal number — re-bias the exponent. */
        exponent32 = (uint32_t)exponent16 - FLOAT16_EXPONENT_BIAS + FLOAT32_EXPONENT_BIAS;
    }

    /* Compose. */
    const uint32_t sign32_shifted = (uint32_t)sign16_shifted
            << (FLOAT32_SIGN_BIT_POSITION - FLOAT16_SIGN_BIT_POSITION);
    const uint32_t exponent32_shifted = exponent32 << FLOAT32_EXPONENT_BIT_POSITION;
    const uint32_t float32_value = sign32_shifted | exponent32_shifted | significand32;

    return zserio_float_from_u32(float32_value);
}

/* ------------------------------------------------------------------------ */
/* float32 -> float16                                                       */
/* ------------------------------------------------------------------------ */

uint16_t zserio_float_to_u16(float v)
{
    const uint32_t float32_value = zserio_float_to_u32(v);

    /* Decompose. */
    const uint32_t sign32_shifted = (float32_value & FLOAT32_SIGN_MASK);
    const uint32_t exponent32 = (float32_value & FLOAT32_EXPONENT_MASK) >> FLOAT32_EXPONENT_BIT_POSITION;
    const uint32_t significand32 = (float32_value & FLOAT32_SIGNIFICAND_MASK);

    /* Compute half-precision significand by truncating right-shift. */
    uint16_t significand16 = (uint16_t)
            (significand32 >> (FLOAT32_SIGNIFICAND_NUM_BITS - FLOAT16_SIGNIFICAND_NUM_BITS));

    bool needs_rounding = false;
    uint16_t exponent16 = 0U;
    if (exponent32 == 0U) {
        if (significand32 != 0U) {
            /* Subnormal float32 → underflow to zero in float16. */
            significand16 = 0U;
        }
    } else if (exponent32 == FLOAT32_EXPONENT_INFINITY_NAN) {
        /* Infinity or NaN. */
        exponent16 = FLOAT16_EXPONENT_INFINITY_NAN;
    } else {
        /* Normal number — re-bias and check for overflow / underflow. */
        const int32_t signed_exponent16 =
                (int32_t)exponent32 - (int32_t)FLOAT32_EXPONENT_BIAS + (int32_t)FLOAT16_EXPONENT_BIAS;
        if (signed_exponent16 > (int32_t)FLOAT16_EXPONENT_INFINITY_NAN) {
            /* Overflow → infinity. */
            exponent16 = FLOAT16_EXPONENT_INFINITY_NAN;
        } else if (signed_exponent16 <= 0) {
            /* Underflow → subnormal or zero. */
            if (signed_exponent16 <= -(int32_t)FLOAT16_SIGNIFICAND_NUM_BITS) {
                /* Too small even for subnormal. */
                significand16 = 0U;
            } else {
                /* Build a subnormal float16. */
                const uint32_t full_significand32 = significand32 | (FLOAT32_SIGNIFICAND_MASK + 1U);
                const uint32_t significand_shift = (uint32_t)((int32_t)1 - signed_exponent16);
                const uint32_t shift_amount =
                        FLOAT32_SIGNIFICAND_NUM_BITS - FLOAT16_SIGNIFICAND_NUM_BITS + significand_shift;
                significand16 = (uint16_t)(full_significand32 >> shift_amount);

                needs_rounding =
                        ((full_significand32 >> (shift_amount - 1U)) & 1U) != 0U;
            }
        } else {
            /* Exponent in range. */
            exponent16 = (uint16_t)signed_exponent16;
            needs_rounding =
                    ((significand32 >> (FLOAT32_SIGNIFICAND_NUM_BITS - FLOAT16_SIGNIFICAND_NUM_BITS - 1U))
                            & 1U) != 0U;
        }
    }

    /* Compose. */
    const uint16_t sign16_shifted = (uint16_t)
            (sign32_shifted >> (FLOAT32_SIGN_BIT_POSITION - FLOAT16_SIGN_BIT_POSITION));
    const uint16_t exponent16_shifted = (uint16_t)(exponent16 << FLOAT16_EXPONENT_BIT_POSITION);
    uint16_t float16_value = (uint16_t)(sign16_shifted | exponent16_shifted) | significand16;

    /* Round-to-nearest (may overflow into infinity, which is fine). */
    if (needs_rounding) {
        float16_value = (uint16_t)(float16_value + 1U);
    }
    return float16_value;
}

/* ------------------------------------------------------------------------ */
/* Write                                                                    */
/*                                                                           */
/* Reads are `static inline` in float.h (see the rationale there).           */
/* ------------------------------------------------------------------------ */

zserio_status_t zserio_write_float16(zserio_bitstream_writer_t *bs, float v)
{
    return zserio_bitstream_write_bits(bs, 16U, (uint64_t)zserio_float_to_u16(v));
}

zserio_status_t zserio_write_float32(zserio_bitstream_writer_t *bs, float v)
{
    return zserio_bitstream_write_bits(bs, 32U, (uint64_t)zserio_float_to_u32(v));
}

zserio_status_t zserio_write_float64(zserio_bitstream_writer_t *bs, double v)
{
    return zserio_bitstream_write_bits(bs, 64U, zserio_double_to_u64(v));
}
