/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_EXPRESSIONS_H_
#define ZSERIO_EXPRESSIONS_H_

/**
 * @file
 * Inline helpers used by generated C99 code to lower zserio
 * expression-language built-ins that don't have a one-to-one C
 * operator equivalent. Each helper is a `static inline` so the
 * generated code stays free of separate translation-unit linkage
 * for these tiny utilities.
 *
 * Generated code calls these via the C99 generator's
 * `CExpressionFormattingPolicy` — most of the rest of the zserio
 * expression surface (arithmetic, comparison, bitwise) lowers to
 * C operators directly without a helper.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Number of bits needed to represent `value` as an unsigned integer.
 *
 * Mirrors zserio's {@code numbits(x)}. Both treat 0 as needing 1 bit
 * — the bit is the literal 0.
 */
static inline uint8_t zserio_num_bits(uint64_t value)
{
    uint8_t n = 1U;
    uint64_t v = value >> 1;
    while (v > 0U) {
        ++n;
        v >>= 1;
    }
    return n;
}

/**
 * Test whether all bits of `flags` are set in `bitmask`.
 *
 * Mirrors zserio's {@code isset(bitmask, flags)}. Both expect
 * `flags` to be a single flag (or an OR of flags) from the same
 * bitmask type as `bitmask`.
 */
static inline bool zserio_isset(uint64_t bitmask, uint64_t flags)
{
    return (bitmask & flags) == flags;
}

#ifdef __cplusplus
}
#endif

#endif /* ZSERIO_EXPRESSIONS_H_ */
