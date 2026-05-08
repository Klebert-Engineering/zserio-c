/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_STATUS_H_
#define ZSERIO_STATUS_H_

/**
 * @file
 * Status codes returned by the zserio-c runtime and generated code.
 *
 * Every fallible function returns `zserio_status_t`. Output values
 * are written through pointer parameters and are valid only when the
 * returned status is `ZSERIO_OK`. Callers MUST check the status
 * before reading any output. See REQ-FS-002.
 *
 * The mapping from this enumeration to the upstream zserio C++
 * exception classes it replaces is recorded in
 * `tests/conformance/error_mapping.csv`.
 */

/**
 * Status enumeration.
 *
 * `ZSERIO_OK` is guaranteed to be zero so that
 * `if (status) handle_error();` works as expected.
 *
 * The non-zero values are organized into four bands:
 * - argument validation (caller passed something invalid)
 * - stream / buffer state (encoder ran out of room, decoder ran out
 *   of input, etc.)
 * - encoding semantics (the bytes don't represent a valid value of
 *   the schema's type)
 * - resource (allocator returned NULL)
 *
 * The numeric values of the non-OK members are NOT part of the API
 * stability contract; do not switch on them. Switch on the
 * enumerator names.
 */
typedef enum zserio_status {
    ZSERIO_OK = 0,

    /* Argument validation at the public API boundary (REQ-FS-006). */
    ZSERIO_ERR_INVALID_ARGUMENT,        /**< NULL pointer, zero-size buffer, missing vtable, etc. */
    ZSERIO_ERR_INVALID_NUM_BITS,        /**< Bit width outside the valid range [1, 64]. */
    ZSERIO_ERR_INVALID_BUFFER,          /**< Buffer / bit-size mismatch on bitstream construction. */
    ZSERIO_ERR_INVALID_POSITION,        /**< Bit position seek with out-of-range value. */
    ZSERIO_ERR_VALUE_OUT_OF_RANGE,      /**< Numeric input does not fit the target type or encoding. */

    /* Stream and buffer state. */
    ZSERIO_ERR_END_OF_STREAM,           /**< Read attempted past buffer end. */
    ZSERIO_ERR_BUFFER_OVERFLOW,         /**< Write would exceed buffer capacity. */
    ZSERIO_ERR_BUFFER_UNDERFLOW,        /**< Read consumed fewer bits than required by the schema. */
    ZSERIO_ERR_BUFFER_TOO_LARGE,        /**< Construction with buffer size > runtime maximum. */

    /* Encoding / decoding semantics. */
    ZSERIO_ERR_INVALID_ENCODING,        /**< Malformed varint, varsize out-of-range, etc. */
    ZSERIO_ERR_INVALID_ENUM,            /**< Decoded integer is not a declared enumerator. */
    ZSERIO_ERR_INVALID_TAG,             /**< Union branch tag out of range. */
    ZSERIO_ERR_CONSTRAINT_VIOLATED,     /**< Schema constraint expression evaluated to false. */
    ZSERIO_ERR_OFFSET_MISMATCH,         /**< Encoded offset disagrees with actual bit position. */
    ZSERIO_ERR_ARRAY_TOO_LARGE,         /**< Decoded array length exceeds caller-supplied max_count. */

    /* Resource. */
    ZSERIO_ERR_OUT_OF_MEMORY            /**< Allocator returned NULL. */
} zserio_status_t;

/**
 * `ZSERIO_TRY(expr)` — early-return-on-error helper.
 *
 * Evaluates `expr` (which must produce a `zserio_status_t`) and
 * returns from the enclosing function with that status if it is
 * not `ZSERIO_OK`.
 *
 * Implementation note: the macro declares the temporary identifier
 * `zserio_try_st` in a `do { ... } while (0)` block. Do not use
 * that name in any block that contains a `ZSERIO_TRY` invocation.
 */
#define ZSERIO_TRY(expr)                                          \
    do {                                                          \
        const zserio_status_t zserio_try_st = (expr);             \
        if (zserio_try_st != ZSERIO_OK) {                         \
            return zserio_try_st;                                 \
        }                                                         \
    } while (0)

#endif /* ZSERIO_STATUS_H_ */
