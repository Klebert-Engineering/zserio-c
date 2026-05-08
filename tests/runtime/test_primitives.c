/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include <stdint.h>
#include <stdbool.h>

#include "unity.h"
#include "zserio/bitstream.h"
#include "zserio/primitives.h"
#include "zserio/status.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* Wire layout — big-endian for every fixed-width type (REQ-FN-001).        */
/* ------------------------------------------------------------------------ */

static void test_uint8_byte_layout(void)
{
    /* Verifies: REQ-FN-001 */
    const uint8_t buf[1] = { 0xAB };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    uint8_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint8(&r, &v));
    TEST_ASSERT_EQUAL_HEX8(0xAB, v);
}

static void test_uint16_byte_layout(void)
{
    /* Verifies: REQ-FN-001
     * Big-endian: byte 0 is the high byte. 0xCAFE = { 0xCA, 0xFE }. */
    const uint8_t buf[2] = { 0xCA, 0xFE };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    uint16_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint16(&r, &v));
    TEST_ASSERT_EQUAL_HEX16(0xCAFE, v);
}

static void test_uint32_byte_layout(void)
{
    /* Verifies: REQ-FN-001 */
    const uint8_t buf[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 32U);
    uint32_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint32(&r, &v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, v);
}

static void test_uint64_byte_layout(void)
{
    /* Verifies: REQ-FN-001 */
    const uint8_t buf[8] = { 0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 64U);
    uint64_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint64(&r, &v));
    TEST_ASSERT_EQUAL_HEX64(0x0123456789ABCDEFULL, v);
}

static void test_int16_byte_layout_negative(void)
{
    /* Verifies: REQ-FN-001
     * Encoding Guide example: -513 as int16 is { 0xFD, 0xFF }. */
    const uint8_t buf[2] = { 0xFD, 0xFF };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    int16_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int16(&r, &v));
    TEST_ASSERT_EQUAL_INT16(-513, v);
}

static void test_int16_byte_layout_positive(void)
{
    /* Verifies: REQ-FN-001
     * Encoding Guide example: 513 as int16 is { 0x02, 0x01 }. */
    const uint8_t buf[2] = { 0x02, 0x01 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    int16_t v = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int16(&r, &v));
    TEST_ASSERT_EQUAL_INT16(513, v);
}

/* ------------------------------------------------------------------------ */
/* Round-trip tests for each unsigned width                                 */
/* ------------------------------------------------------------------------ */

static void test_uint8_round_trip_extremes(void)
{
    uint8_t buf[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 24U);
    (void)zserio_write_uint8(&w, 0U);
    (void)zserio_write_uint8(&w, 0xFFU);
    (void)zserio_write_uint8(&w, 0x42U);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 24U);
    uint8_t a = 0U, b = 0U, c = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint8(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint8(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint8(&r, &c));
    TEST_ASSERT_EQUAL_HEX8(0x00, a);
    TEST_ASSERT_EQUAL_HEX8(0xFF, b);
    TEST_ASSERT_EQUAL_HEX8(0x42, c);
}

static void test_uint16_round_trip_extremes(void)
{
    uint8_t buf[6] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 48U);
    (void)zserio_write_uint16(&w, 0U);
    (void)zserio_write_uint16(&w, UINT16_MAX);
    (void)zserio_write_uint16(&w, 0xCAFEU);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 48U);
    uint16_t a = 0U, b = 0U, c = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint16(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint16(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint16(&r, &c));
    TEST_ASSERT_EQUAL_HEX16(0x0000, a);
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, b);
    TEST_ASSERT_EQUAL_HEX16(0xCAFE, c);
}

static void test_uint32_round_trip_extremes(void)
{
    uint8_t buf[12] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 96U);
    (void)zserio_write_uint32(&w, 0U);
    (void)zserio_write_uint32(&w, UINT32_MAX);
    (void)zserio_write_uint32(&w, 0xDEADBEEFU);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 96U);
    uint32_t a = 0U, b = 0U, c = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint32(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint32(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint32(&r, &c));
    TEST_ASSERT_EQUAL_HEX32(0x00000000U, a);
    TEST_ASSERT_EQUAL_HEX32(0xFFFFFFFFU, b);
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFU, c);
}

static void test_uint64_round_trip_extremes(void)
{
    uint8_t buf[24] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 192U);
    (void)zserio_write_uint64(&w, 0U);
    (void)zserio_write_uint64(&w, UINT64_MAX);
    (void)zserio_write_uint64(&w, 0x0123456789ABCDEFULL);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 192U);
    uint64_t a = 0U, b = 0U, c = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint64(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint64(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_uint64(&r, &c));
    TEST_ASSERT_EQUAL_HEX64(UINT64_C(0), a);
    TEST_ASSERT_EQUAL_HEX64(UINT64_MAX, b);
    TEST_ASSERT_EQUAL_HEX64(0x0123456789ABCDEFULL, c);
}

/* ------------------------------------------------------------------------ */
/* Round-trip tests for each signed width                                   */
/* ------------------------------------------------------------------------ */

static void test_int8_round_trip_extremes(void)
{
    uint8_t buf[3] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 24U);
    (void)zserio_write_int8(&w, INT8_MIN);
    (void)zserio_write_int8(&w, INT8_MAX);
    (void)zserio_write_int8(&w, -1);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 24U);
    int8_t a = 0, b = 0, c = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int8(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int8(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int8(&r, &c));
    TEST_ASSERT_EQUAL_INT8(INT8_MIN, a);
    TEST_ASSERT_EQUAL_INT8(INT8_MAX, b);
    TEST_ASSERT_EQUAL_INT8(-1, c);
}

static void test_int16_round_trip_extremes(void)
{
    uint8_t buf[6] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 48U);
    (void)zserio_write_int16(&w, INT16_MIN);
    (void)zserio_write_int16(&w, INT16_MAX);
    (void)zserio_write_int16(&w, -513);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 48U);
    int16_t a = 0, b = 0, c = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int16(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int16(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int16(&r, &c));
    TEST_ASSERT_EQUAL_INT16(INT16_MIN, a);
    TEST_ASSERT_EQUAL_INT16(INT16_MAX, b);
    TEST_ASSERT_EQUAL_INT16(-513, c);
}

static void test_int32_round_trip_extremes(void)
{
    uint8_t buf[12] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 96U);
    (void)zserio_write_int32(&w, INT32_MIN);
    (void)zserio_write_int32(&w, INT32_MAX);
    (void)zserio_write_int32(&w, -1054780911);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 96U);
    int32_t a = 0, b = 0, c = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int32(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int32(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int32(&r, &c));
    TEST_ASSERT_EQUAL_INT32(INT32_MIN, a);
    TEST_ASSERT_EQUAL_INT32(INT32_MAX, b);
    TEST_ASSERT_EQUAL_INT32(-1054780911, c);
}

static void test_int64_round_trip_extremes(void)
{
    uint8_t buf[24] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 192U);
    (void)zserio_write_int64(&w, INT64_MIN);
    (void)zserio_write_int64(&w, INT64_MAX);
    (void)zserio_write_int64(&w, -1);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 192U);
    int64_t a = 0, b = 0, c = 0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int64(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int64(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_int64(&r, &c));
    TEST_ASSERT_EQUAL_INT64(INT64_MIN, a);
    TEST_ASSERT_EQUAL_INT64(INT64_MAX, b);
    TEST_ASSERT_EQUAL_INT64(-1, c);
}

/* ------------------------------------------------------------------------ */
/* Bool                                                                     */
/* ------------------------------------------------------------------------ */

static void test_bool_round_trip(void)
{
    /* Verifies: REQ-FN-001 — bool is exactly one bit. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    (void)zserio_write_bool(&w, true);
    (void)zserio_write_bool(&w, false);
    (void)zserio_write_bool(&w, true);
    (void)zserio_write_bool(&w, true);
    /* 4 bools written → 4 bits → byte = 1011_0000 = 0xB0 */
    TEST_ASSERT_EQUAL_HEX8(0xB0, buf[0]);
    TEST_ASSERT_EQUAL_size_t(4U, zserio_bitstream_writer_position(&w));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 4U);
    bool a = false, b = false, c = false, d = false;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &a));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &b));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &c));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &d));
    TEST_ASSERT_TRUE(a);
    TEST_ASSERT_FALSE(b);
    TEST_ASSERT_TRUE(c);
    TEST_ASSERT_TRUE(d);
}

static void test_bool_one_bit_consumed(void)
{
    /* Verifies: REQ-FN-001 — exactly one bit, not a byte. */
    const uint8_t buf[1] = { 0xC0 }; /* 1100_0000: bit 0 is 1, bit 1 is 1, bit 2 is 0 */
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    bool b = false;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_EQUAL_size_t(1U, zserio_bitstream_reader_position(&r));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &b));
    TEST_ASSERT_TRUE(b);
    TEST_ASSERT_EQUAL_size_t(2U, zserio_bitstream_reader_position(&r));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_bool(&r, &b));
    TEST_ASSERT_FALSE(b);
}

/* ------------------------------------------------------------------------ */
/* NULL output checks (REQ-FS-006)                                          */
/* ------------------------------------------------------------------------ */

static void test_read_null_out(void)
{
    /* Verifies: REQ-FS-006 — every reader rejects NULL out. */
    const uint8_t buf[8] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 64U);

    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_uint8 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_uint16(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_uint32(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_uint64(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_int8  (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_int16 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_int32 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_int64 (&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_bool  (&r, NULL));

    /* Position is unchanged on failure. */
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r));
}

/* ------------------------------------------------------------------------ */
/* Propagation of underlying errors                                         */
/* ------------------------------------------------------------------------ */

static void test_read_eof_propagation(void)
{
    /* Verifies: REQ-FS-005 (bounded reads) */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 4U);
    /* Buffer holds only 4 bits; reading uint8 needs 8. */
    uint8_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM, zserio_read_uint8(&r, &v));
}

static void test_write_overflow_propagation(void)
{
    /* Verifies: REQ-FS-005, REQ-FS-006 */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 4U);
    /* Capacity is 4 bits; writing uint8 needs 8. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_BUFFER_OVERFLOW, zserio_write_uint8(&w, 0xABU));
}

static void test_write_null_bs_propagation(void)
{
    /* Verifies: REQ-FS-006 — writers propagate NULL bs from bitstream. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_uint8 (NULL, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_uint16(NULL, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_uint32(NULL, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_uint64(NULL, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_int8  (NULL, 0));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_int16 (NULL, 0));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_int32 (NULL, 0));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_int64 (NULL, 0));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_write_bool  (NULL, false));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    /* Wire layout (big-endian sanity) */
    RUN_TEST(test_uint8_byte_layout);
    RUN_TEST(test_uint16_byte_layout);
    RUN_TEST(test_uint32_byte_layout);
    RUN_TEST(test_uint64_byte_layout);
    RUN_TEST(test_int16_byte_layout_negative);
    RUN_TEST(test_int16_byte_layout_positive);

    /* Round-trip extremes */
    RUN_TEST(test_uint8_round_trip_extremes);
    RUN_TEST(test_uint16_round_trip_extremes);
    RUN_TEST(test_uint32_round_trip_extremes);
    RUN_TEST(test_uint64_round_trip_extremes);
    RUN_TEST(test_int8_round_trip_extremes);
    RUN_TEST(test_int16_round_trip_extremes);
    RUN_TEST(test_int32_round_trip_extremes);
    RUN_TEST(test_int64_round_trip_extremes);

    /* Bool */
    RUN_TEST(test_bool_round_trip);
    RUN_TEST(test_bool_one_bit_consumed);

    /* Defensive */
    RUN_TEST(test_read_null_out);
    RUN_TEST(test_read_eof_propagation);
    RUN_TEST(test_write_overflow_propagation);
    RUN_TEST(test_write_null_bs_propagation);

    return UNITY_END();
}
