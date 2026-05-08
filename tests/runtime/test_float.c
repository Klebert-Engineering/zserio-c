/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

/*
 * All comparisons in this file are on integer bit patterns, never on
 * float / double values directly. This avoids the host's NaN-not-equal-
 * NaN behavior and keeps the test suite clean under -Wfloat-equal.
 * It also tests exactly the property we care about for wire-format
 * compatibility: byte-identical output for byte-identical input.
 */

#include <stdint.h>

#include "unity.h"
#include "zserio/bitstream.h"
#include "zserio/float.h"
#include "zserio/status.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* float32 round-trip                                                       */
/* ------------------------------------------------------------------------ */

static void roundtrip_float32_pattern(uint32_t bits)
{
    /* Verifies: REQ-FN-001 — float32 wire format is the bit pattern, BE. */
    uint8_t buf[4] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 32U);
    const float v = zserio_float_from_u32(bits);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_float32(&w, v));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 32U);
    float got = 0.0F;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_float32(&r, &got));
    TEST_ASSERT_EQUAL_HEX32(bits, zserio_float_to_u32(got));
}

static void test_float32_zero(void)              { roundtrip_float32_pattern(0x00000000U); }
static void test_float32_negative_zero(void)     { roundtrip_float32_pattern(0x80000000U); }
static void test_float32_one(void)               { roundtrip_float32_pattern(0x3F800000U); /* 1.0 */ }
static void test_float32_negative_one(void)      { roundtrip_float32_pattern(0xBF800000U); /* -1.0 */ }
static void test_float32_pi(void)                { roundtrip_float32_pattern(0x40490FDBU); /* π */ }
static void test_float32_positive_infinity(void) { roundtrip_float32_pattern(0x7F800000U); }
static void test_float32_negative_infinity(void) { roundtrip_float32_pattern(0xFF800000U); }
static void test_float32_quiet_nan(void)         { roundtrip_float32_pattern(0x7FC00000U); }
static void test_float32_subnormal_min(void)     { roundtrip_float32_pattern(0x00000001U); }
static void test_float32_max(void)               { roundtrip_float32_pattern(0x7F7FFFFFU); }

/* ------------------------------------------------------------------------ */
/* float64 round-trip                                                       */
/* ------------------------------------------------------------------------ */

static void roundtrip_float64_pattern(uint64_t bits)
{
    /* Verifies: REQ-FN-001 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    const double v = zserio_double_from_u64(bits);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_float64(&w, v));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 64U);
    double got = 0.0;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_float64(&r, &got));
    TEST_ASSERT_EQUAL_HEX64(bits, zserio_double_to_u64(got));
}

static void test_float64_zero(void)              { roundtrip_float64_pattern(UINT64_C(0)); }
static void test_float64_negative_zero(void)     { roundtrip_float64_pattern(UINT64_C(0x8000000000000000)); }
static void test_float64_one(void)               { roundtrip_float64_pattern(UINT64_C(0x3FF0000000000000)); }
static void test_float64_pi(void)                { roundtrip_float64_pattern(UINT64_C(0x400921FB54442D18)); }
static void test_float64_positive_infinity(void) { roundtrip_float64_pattern(UINT64_C(0x7FF0000000000000)); }
static void test_float64_quiet_nan(void)         { roundtrip_float64_pattern(UINT64_C(0x7FF8000000000000)); }
static void test_float64_subnormal_min(void)     { roundtrip_float64_pattern(UINT64_C(0x0000000000000001)); }
static void test_float64_max(void)               { roundtrip_float64_pattern(UINT64_C(0x7FEFFFFFFFFFFFFF)); }

/* ------------------------------------------------------------------------ */
/* float16 round-trip via the conversion helpers                             */
/* ------------------------------------------------------------------------ */

static void roundtrip_float16_pattern(uint16_t bits)
{
    /* Verifies: REQ-FN-001 — float16 wire format is the half-precision
     * bit pattern. The round-trip goes float16 -> float32 -> stream ->
     * float32 -> float16, which loses no information for bit patterns
     * representable in float16 (the conversion to float32 is exact for
     * normals, infinities, and zeros). */
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    const float v = zserio_float_from_u16(bits);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_float16(&w, v));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 16U);
    float got = 0.0F;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_float16(&r, &got));
    /* Convert back to half-precision bits and compare. */
    TEST_ASSERT_EQUAL_HEX16(bits, zserio_float_to_u16(got));
}

static void test_float16_zero(void)              { roundtrip_float16_pattern(0x0000U); }
static void test_float16_negative_zero(void)     { roundtrip_float16_pattern(0x8000U); }
static void test_float16_one(void)               { roundtrip_float16_pattern(0x3C00U); }
static void test_float16_negative_one(void)      { roundtrip_float16_pattern(0xBC00U); }
static void test_float16_positive_infinity(void) { roundtrip_float16_pattern(0x7C00U); }
static void test_float16_negative_infinity(void) { roundtrip_float16_pattern(0xFC00U); }
static void test_float16_max(void)               { roundtrip_float16_pattern(0x7BFFU); /* ~65504 */ }
static void test_float16_smallest_normal(void)   { roundtrip_float16_pattern(0x0400U); }

/* ------------------------------------------------------------------------ */
/* float16 wire-format spot checks                                          */
/* ------------------------------------------------------------------------ */

static void test_float16_eight_point_zero_byte_layout(void)
{
    /* Verifies: REQ-FN-001 — Encoding Guide example: 8.0 as float16 is
     * { 0x48, 0x00 } on the wire. */
    uint8_t buf[2] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 16U);
    /* 8.0 in half precision: sign=0, exponent=0b10010=18 (bias 15 → 2^3),
     * significand=0. Bit pattern: 0_10010_0000000000 = 0x4800. */
    const float v = zserio_float_from_u16(0x4800U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_float16(&w, v));
    TEST_ASSERT_EQUAL_HEX8(0x48, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
}

static void test_float16_subnormal_widens_to_float32_normal(void)
{
    /* A float16 subnormal (exponent=0, significand≠0) is normalized to a
     * float32 normal. Round-tripping via float32 then back to float16
     * must reproduce the original half-bits. */
    roundtrip_float16_pattern(0x0001U); /* smallest positive subnormal */
    roundtrip_float16_pattern(0x03FFU); /* largest subnormal */
}

/* ------------------------------------------------------------------------ */
/* Conversion helpers — symmetric                                           */
/* ------------------------------------------------------------------------ */

static void test_float32_helpers_symmetric(void)
{
    const uint32_t patterns[] = {
        0x00000000U, 0x80000000U, 0x3F800000U, 0xBF800000U,
        0x7F800000U, 0x7FC00000U, 0x00000001U, 0x7F7FFFFFU
    };
    const size_t n = sizeof(patterns) / sizeof(patterns[0]);
    for (size_t i = 0U; i < n; i++) {
        const uint32_t b = patterns[i];
        TEST_ASSERT_EQUAL_HEX32(b, zserio_float_to_u32(zserio_float_from_u32(b)));
    }
}

static void test_float64_helpers_symmetric(void)
{
    const uint64_t patterns[] = {
        UINT64_C(0),
        UINT64_C(0x8000000000000000),
        UINT64_C(0x3FF0000000000000),
        UINT64_C(0x7FF0000000000000),
        UINT64_C(0x7FF8000000000000),
        UINT64_C(0x0000000000000001),
        UINT64_C(0x7FEFFFFFFFFFFFFF)
    };
    const size_t n = sizeof(patterns) / sizeof(patterns[0]);
    for (size_t i = 0U; i < n; i++) {
        const uint64_t b = patterns[i];
        TEST_ASSERT_EQUAL_HEX64(b, zserio_double_to_u64(zserio_double_from_u64(b)));
    }
}

/* ------------------------------------------------------------------------ */
/* Defensive checks                                                         */
/* ------------------------------------------------------------------------ */

static void test_read_null_out(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[8] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 64U);
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_float16(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_float32(&r, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT, zserio_read_float64(&r, NULL));
}

static void test_read_eof_propagation(void)
{
    /* Verifies: REQ-FS-005 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    float f = 0.0F;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM, zserio_read_float16(&r, &f));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    /* float32 round-trip */
    RUN_TEST(test_float32_zero);
    RUN_TEST(test_float32_negative_zero);
    RUN_TEST(test_float32_one);
    RUN_TEST(test_float32_negative_one);
    RUN_TEST(test_float32_pi);
    RUN_TEST(test_float32_positive_infinity);
    RUN_TEST(test_float32_negative_infinity);
    RUN_TEST(test_float32_quiet_nan);
    RUN_TEST(test_float32_subnormal_min);
    RUN_TEST(test_float32_max);

    /* float64 round-trip */
    RUN_TEST(test_float64_zero);
    RUN_TEST(test_float64_negative_zero);
    RUN_TEST(test_float64_one);
    RUN_TEST(test_float64_pi);
    RUN_TEST(test_float64_positive_infinity);
    RUN_TEST(test_float64_quiet_nan);
    RUN_TEST(test_float64_subnormal_min);
    RUN_TEST(test_float64_max);

    /* float16 round-trip */
    RUN_TEST(test_float16_zero);
    RUN_TEST(test_float16_negative_zero);
    RUN_TEST(test_float16_one);
    RUN_TEST(test_float16_negative_one);
    RUN_TEST(test_float16_positive_infinity);
    RUN_TEST(test_float16_negative_infinity);
    RUN_TEST(test_float16_max);
    RUN_TEST(test_float16_smallest_normal);
    RUN_TEST(test_float16_eight_point_zero_byte_layout);
    RUN_TEST(test_float16_subnormal_widens_to_float32_normal);

    /* Conversion helpers */
    RUN_TEST(test_float32_helpers_symmetric);
    RUN_TEST(test_float64_helpers_symmetric);

    /* Defensive */
    RUN_TEST(test_read_null_out);
    RUN_TEST(test_read_eof_propagation);

    return UNITY_END();
}
