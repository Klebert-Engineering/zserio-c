/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include <stdint.h>

#include "unity.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"
#include "zserio/varint.h"

void setUp(void) {}
void tearDown(void) {}

/* Convenience: round-trip a uint64 through varuint and assert equality. */
#define ROUND_TRIP_UINT(write_fn, read_fn, type, val, capacity_bits)        \
    do {                                                                    \
        uint8_t buf[((capacity_bits) + 7U) / 8U] = { 0 };                   \
        zserio_bitstream_writer_t w;                                        \
        TEST_ASSERT_EQUAL(ZSERIO_OK,                                        \
            zserio_bitstream_writer_init(&w, buf, (capacity_bits)));        \
        TEST_ASSERT_EQUAL(ZSERIO_OK, write_fn(&w, (val)));                  \
        zserio_bitstream_reader_t r;                                        \
        (void)zserio_bitstream_reader_init(&r, buf,                         \
            zserio_bitstream_writer_position(&w));                          \
        type got = (type)0;                                                 \
        TEST_ASSERT_EQUAL(ZSERIO_OK, read_fn(&r, &got));                    \
        TEST_ASSERT_EQUAL((val), got);                                      \
    } while (0)

#define ROUND_TRIP_INT(write_fn, read_fn, type, val, capacity_bits)         \
    do {                                                                    \
        uint8_t buf[((capacity_bits) + 7U) / 8U] = { 0 };                   \
        zserio_bitstream_writer_t w;                                        \
        TEST_ASSERT_EQUAL(ZSERIO_OK,                                        \
            zserio_bitstream_writer_init(&w, buf, (capacity_bits)));        \
        TEST_ASSERT_EQUAL(ZSERIO_OK, write_fn(&w, (val)));                  \
        zserio_bitstream_reader_t r;                                        \
        (void)zserio_bitstream_reader_init(&r, buf,                         \
            zserio_bitstream_writer_position(&w));                          \
        type got = (type)0;                                                 \
        TEST_ASSERT_EQUAL(ZSERIO_OK, read_fn(&r, &got));                    \
        TEST_ASSERT_EQUAL((val), got);                                      \
    } while (0)

/* ------------------------------------------------------------------------ */
/* varuint16                                                                */
/* ------------------------------------------------------------------------ */

static void test_varuint16_round_trip(void)
{
    /* Verifies: REQ-FN-001 */
    ROUND_TRIP_UINT(zserio_write_varuint16, zserio_read_varuint16, uint16_t, 0U, 16U);
    ROUND_TRIP_UINT(zserio_write_varuint16, zserio_read_varuint16, uint16_t, 1U, 16U);
    ROUND_TRIP_UINT(zserio_write_varuint16, zserio_read_varuint16, uint16_t, 127U, 16U);
    ROUND_TRIP_UINT(zserio_write_varuint16, zserio_read_varuint16, uint16_t, 128U, 16U);
    ROUND_TRIP_UINT(zserio_write_varuint16, zserio_read_varuint16, uint16_t, 32767U, 16U);
}

static void test_varuint16_byte_count(void)
{
    /* Verifies: REQ-FN-001 — values ≤ 127 use 1 byte, larger use 2. */
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    (void)zserio_write_varuint16(&w, 127U);
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_writer_position(&w));

    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    (void)zserio_write_varuint16(&w, 128U);
    TEST_ASSERT_EQUAL_size_t(16U, zserio_bitstream_writer_position(&w));
}

static void test_varuint16_out_of_range(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varuint16(&w, 32768U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varuint16(&w, UINT16_MAX));
}

/* ------------------------------------------------------------------------ */
/* varuint32                                                                */
/* ------------------------------------------------------------------------ */

static void test_varuint32_round_trip(void)
{
    /* Verifies: REQ-FN-001 */
    ROUND_TRIP_UINT(zserio_write_varuint32, zserio_read_varuint32, uint32_t, 0U, 32U);
    ROUND_TRIP_UINT(zserio_write_varuint32, zserio_read_varuint32, uint32_t, 127U, 32U);
    ROUND_TRIP_UINT(zserio_write_varuint32, zserio_read_varuint32, uint32_t, 16383U, 32U);
    ROUND_TRIP_UINT(zserio_write_varuint32, zserio_read_varuint32, uint32_t, 2097151U, 32U);
    ROUND_TRIP_UINT(zserio_write_varuint32, zserio_read_varuint32, uint32_t,
                    ((uint32_t)1U << 29) - 1U, 32U);
}

static void test_varuint32_out_of_range(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[4] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 32U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varuint32(&w, (uint32_t)1U << 29));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varuint32(&w, UINT32_MAX));
}

/* ------------------------------------------------------------------------ */
/* varuint64                                                                */
/* ------------------------------------------------------------------------ */

static void test_varuint64_round_trip(void)
{
    /* Verifies: REQ-FN-001 */
    ROUND_TRIP_UINT(zserio_write_varuint64, zserio_read_varuint64, uint64_t,
                    UINT64_C(0), 64U);
    ROUND_TRIP_UINT(zserio_write_varuint64, zserio_read_varuint64, uint64_t,
                    UINT64_C(127), 64U);
    ROUND_TRIP_UINT(zserio_write_varuint64, zserio_read_varuint64, uint64_t,
                    ((uint64_t)1U << 35) - 1U, 64U);
    ROUND_TRIP_UINT(zserio_write_varuint64, zserio_read_varuint64, uint64_t,
                    ((uint64_t)1U << 57) - 1U, 64U);
}

static void test_varuint64_out_of_range(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varuint64(&w, (uint64_t)1U << 57));
}

/* ------------------------------------------------------------------------ */
/* varuint (full uint64)                                                    */
/* ------------------------------------------------------------------------ */

static void test_varuint_round_trip(void)
{
    /* Verifies: REQ-FN-001 — including UINT64_MAX in the 9-byte encoding. */
    ROUND_TRIP_UINT(zserio_write_varuint, zserio_read_varuint, uint64_t,
                    UINT64_C(0), 72U);
    ROUND_TRIP_UINT(zserio_write_varuint, zserio_read_varuint, uint64_t,
                    UINT64_C(1) << 32, 72U);
    ROUND_TRIP_UINT(zserio_write_varuint, zserio_read_varuint, uint64_t,
                    UINT64_MAX, 72U);
}

static void test_varuint_max_uses_9_bytes(void)
{
    /* Verifies: REQ-FN-001 — UINT64_MAX always serializes as 0xFF×9. */
    uint8_t buf[9] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 72U);
    (void)zserio_write_varuint(&w, UINT64_MAX);
    TEST_ASSERT_EQUAL_size_t(72U, zserio_bitstream_writer_position(&w));
    for (size_t i = 0U; i < 9U; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xFF, buf[i]);
    }
}

/* ------------------------------------------------------------------------ */
/* varsize                                                                  */
/* ------------------------------------------------------------------------ */

static void test_varsize_round_trip(void)
{
    /* Verifies: REQ-FN-001 */
    ROUND_TRIP_UINT(zserio_write_varsize, zserio_read_varsize, uint32_t,
                    0U, 40U);
    ROUND_TRIP_UINT(zserio_write_varsize, zserio_read_varsize, uint32_t,
                    127U, 40U);
    ROUND_TRIP_UINT(zserio_write_varsize, zserio_read_varsize, uint32_t,
                    16383U, 40U);
    ROUND_TRIP_UINT(zserio_write_varsize, zserio_read_varsize, uint32_t,
                    2097151U, 40U);
    ROUND_TRIP_UINT(zserio_write_varsize, zserio_read_varsize, uint32_t,
                    268435455U, 40U);
    ROUND_TRIP_UINT(zserio_write_varsize, zserio_read_varsize, uint32_t,
                    ((uint32_t)1U << 31) - 1U, 40U);
}

static void test_varsize_first_byte_example(void)
{
    /* Verifies: REQ-FN-001 — Encoding Guide example: 2^31-1 starts with 0x83. */
    uint8_t buf[5] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 40U);
    (void)zserio_write_varsize(&w, ((uint32_t)1U << 31) - 1U);
    TEST_ASSERT_EQUAL_size_t(40U, zserio_bitstream_writer_position(&w));
    TEST_ASSERT_EQUAL_HEX8(0x83, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[3]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, buf[4]);
}

static void test_varsize_out_of_range_on_write(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[5] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 40U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varsize(&w, (uint32_t)1U << 31));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varsize(&w, UINT32_MAX));
}

static void test_varsize_invalid_encoding_on_read(void)
{
    /* Verifies: REQ-FS-006 — crafted 5-byte input that decodes above 2^31-1
     * (hand-built: bytes that would decode to 2^36-1 if unconstrained).
     * Each byte sets cont where applicable and value bits all 1. */
    const uint8_t buf[5] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 40U);
    uint32_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ENCODING,
                      zserio_read_varsize(&r, &v));
}

/* ------------------------------------------------------------------------ */
/* varint16                                                                 */
/* ------------------------------------------------------------------------ */

static void test_varint16_round_trip(void)
{
    /* Verifies: REQ-FN-001 */
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t,  0, 16U);
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t,  1, 16U);
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t, -1, 16U);
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t,  63, 16U);
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t, -63, 16U);
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t,  16383, 16U);
    ROUND_TRIP_INT(zserio_write_varint16, zserio_read_varint16, int16_t, -16383, 16U);
}

static void test_varint16_out_of_range(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint16(&w, 16384));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint16(&w, -16384));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint16(&w, INT16_MIN));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint16(&w, INT16_MAX));
}

/* ------------------------------------------------------------------------ */
/* varint32                                                                 */
/* ------------------------------------------------------------------------ */

static void test_varint32_round_trip(void)
{
    /* Verifies: REQ-FN-001 */
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t,  0, 32U);
    /* 2-byte and 3-byte values exercise the multi-byte non-max early-exit
     * path in read_varint_impl (the !n_has_next branch). */
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t,  1000, 32U);
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t, -1000, 32U);
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t,  100000, 32U);
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t, -100000, 32U);
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t,  ((int32_t)1 << 28) - 1, 32U);
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t, -(((int32_t)1 << 28) - 1), 32U);
    ROUND_TRIP_INT(zserio_write_varint32, zserio_read_varint32, int32_t,  -1054780911 / 4, 32U);
}

static void test_varint32_out_of_range(void)
{
    uint8_t buf[4] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 32U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint32(&w, (int32_t)1 << 28));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint32(&w, INT32_MIN));
}

/* ------------------------------------------------------------------------ */
/* varint64                                                                 */
/* ------------------------------------------------------------------------ */

static void test_varint64_round_trip(void)
{
    ROUND_TRIP_INT(zserio_write_varint64, zserio_read_varint64, int64_t,  0, 64U);
    ROUND_TRIP_INT(zserio_write_varint64, zserio_read_varint64, int64_t,
                    ((int64_t)1 << 56) - 1, 64U);
    ROUND_TRIP_INT(zserio_write_varint64, zserio_read_varint64, int64_t,
                    -(((int64_t)1 << 56) - 1), 64U);
}

static void test_varint64_out_of_range(void)
{
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint64(&w, (int64_t)1 << 56));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_varint64(&w, INT64_MIN));
}

/* ------------------------------------------------------------------------ */
/* varint (with INT64_MIN special case)                                     */
/* ------------------------------------------------------------------------ */

static void test_varint_round_trip(void)
{
    /* Verifies: REQ-FN-001 — including INT64_MAX and INT64_MIN. */
    ROUND_TRIP_INT(zserio_write_varint, zserio_read_varint, int64_t,
                    0, 72U);
    ROUND_TRIP_INT(zserio_write_varint, zserio_read_varint, int64_t,
                    INT64_MAX, 72U);
    ROUND_TRIP_INT(zserio_write_varint, zserio_read_varint, int64_t,
                    -INT64_MAX, 72U);
    ROUND_TRIP_INT(zserio_write_varint, zserio_read_varint, int64_t,
                    INT64_MIN, 72U);
}

static void test_varint_int64_min_one_byte(void)
{
    /* Verifies: REQ-FN-001 — INT64_MIN encodes in a single byte 0x80. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    (void)zserio_write_varint(&w, INT64_MIN);
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitstream_writer_position(&w));
    TEST_ASSERT_EQUAL_HEX8(0x80, buf[0]);
}

static void test_varint_max_uses_9_bytes(void)
{
    /* Verifies: REQ-FN-001 — INT64_MAX serializes as the 9-byte form. */
    uint8_t buf[9] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 72U);
    (void)zserio_write_varint(&w, INT64_MAX);
    TEST_ASSERT_EQUAL_size_t(72U, zserio_bitstream_writer_position(&w));
}

/* ------------------------------------------------------------------------ */
/* Defensive checks                                                         */
/* ------------------------------------------------------------------------ */

static void test_read_null_out(void)
{
    /* Verifies: REQ-FS-006 — every reader rejects NULL out. */
    const uint8_t buf[16] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 128U);

    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varuint16(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varuint32(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varuint64(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varuint  (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varsize  (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varint16 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varint32 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varint64 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_varint   (&r, NULL));
}

static void test_read_eof_propagation(void)
{
    /* Verifies: REQ-FS-005 — short buffer triggers END_OF_STREAM. */
    /* 1-byte buffer with cont=1: reader expects more bytes. */
    const uint8_t buf[1] = { 0x80 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    uint16_t v16 = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM, zserio_read_varuint16(&r, &v16));
}

static void test_write_overflow_propagation(void)
{
    /* Verifies: REQ-FS-005 / REQ-FS-006 — small buffer triggers OVERFLOW. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    /* 200 needs 2 bytes of varuint16 → won't fit in 8 bits. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_BUFFER_OVERFLOW,
                      zserio_write_varuint16(&w, 200U));
}

/* ------------------------------------------------------------------------ */
/* Direct zserio_bitsize_var* coverage. The trait round-trip tests in       */
/* test_delta_traits.c hit the common paths; these target the wide-value   */
/* and INT64_MIN edge cases of the byte-count helpers, which the moderate */
/* trait inputs don't reach. */
/* ------------------------------------------------------------------------ */

static void test_bitsize_varuint64_max_uses_all_bytes(void)
{
    /* UINT64_MAX exceeds every VARUINT64_MAX[i] bucket, so
     * bytes_for_unsigned returns the trailing max_bytes value
     * (8 bytes = 64 bits) without finding an earlier fit. */
    TEST_ASSERT_EQUAL_size_t(64U, zserio_bitsize_varuint64(UINT64_MAX));
}

static void test_bitsize_varint_int64_min_single_byte(void)
{
    /* INT64_MIN is the single-byte special form for varint
     * (support_int64_min == true): sign bit set, zero magnitude. */
    TEST_ASSERT_EQUAL_size_t(8U, zserio_bitsize_varint(INT64_MIN));
}

static void test_bitsize_varint64_int64_min_uses_max_bytes(void)
{
    /* INT64_MIN on varint64 (no single-byte special form): magnitude
     * cannot be negated within int64_t, so bytes_for_signed returns
     * max_bytes (8 bytes = 64 bits). */
    TEST_ASSERT_EQUAL_size_t(64U, zserio_bitsize_varint64(INT64_MIN));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_varuint16_round_trip);
    RUN_TEST(test_varuint16_byte_count);
    RUN_TEST(test_varuint16_out_of_range);

    RUN_TEST(test_varuint32_round_trip);
    RUN_TEST(test_varuint32_out_of_range);

    RUN_TEST(test_varuint64_round_trip);
    RUN_TEST(test_varuint64_out_of_range);

    RUN_TEST(test_varuint_round_trip);
    RUN_TEST(test_varuint_max_uses_9_bytes);

    RUN_TEST(test_varsize_round_trip);
    RUN_TEST(test_varsize_first_byte_example);
    RUN_TEST(test_varsize_out_of_range_on_write);
    RUN_TEST(test_varsize_invalid_encoding_on_read);

    RUN_TEST(test_varint16_round_trip);
    RUN_TEST(test_varint16_out_of_range);

    RUN_TEST(test_varint32_round_trip);
    RUN_TEST(test_varint32_out_of_range);

    RUN_TEST(test_varint64_round_trip);
    RUN_TEST(test_varint64_out_of_range);

    RUN_TEST(test_varint_round_trip);
    RUN_TEST(test_varint_int64_min_one_byte);
    RUN_TEST(test_varint_max_uses_9_bytes);

    RUN_TEST(test_read_null_out);
    RUN_TEST(test_read_eof_propagation);
    RUN_TEST(test_write_overflow_propagation);

    RUN_TEST(test_bitsize_varuint64_max_uses_all_bytes);
    RUN_TEST(test_bitsize_varint_int64_min_single_byte);
    RUN_TEST(test_bitsize_varint64_int64_min_uses_max_bytes);

    return UNITY_END();
}
