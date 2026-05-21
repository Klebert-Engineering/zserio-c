/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "zserio/bitstream.h"
#include "zserio/delta.h"
#include "zserio/status.h"
#include "zserio/varint.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* Trait-driven round-trip tests, one per varint flavour. Each one runs the */
/* full lifecycle (init → observe → write → read) through the matching     */
/* zserio_delta_traits_<flavour>, exercising:                               */
/*   - delta.c's traits-mode branches: write_full_element, read_full_element*/
/*     bit_size_unpacked dispatch, and the traits-mode flag updates;        */
/*   - varint.c's generated delta_{read,write,bitsize}_<flavour> wrappers   */
/*     and the underlying zserio_bitsize_<flavour> + bytes_for_* helpers.   */
/* ------------------------------------------------------------------------ */

static void run_round_trip_u64(const zserio_delta_traits_t *traits,
                               const uint64_t *in, size_t n,
                               size_t buf_bits)
{
    uint8_t buf[64] = { 0 };
    TEST_ASSERT_TRUE(buf_bits <= sizeof(buf) * 8U);

    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, buf_bits);
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_init_traits(&ctx, traits));
    for (size_t i = 0U; i < n; i++) {
        TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_observe(&ctx, in[i]));
    }
    for (size_t i = 0U; i < n; i++) {
        TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_write(&ctx, &w, in[i]));
    }
    const size_t total_bits = zserio_bitstream_writer_position(&w);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, total_bits);
    zserio_delta_context_t rctx;
    (void)memset(&rctx, 0, sizeof(rctx));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_init_traits(&rctx, traits));
    for (size_t i = 0U; i < n; i++) {
        uint64_t v = 0U;
        TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_delta_read(&rctx, &r, &v));
        TEST_ASSERT_EQUAL_HEX64(in[i], v);
    }
}

/* Unsigned varint flavours. Values stay well inside each type's encoded
 * range; the goal is to exercise the trait callbacks, not the boundary
 * conditions (those are covered by test_varint). */

static void test_delta_traits_round_trip_varuint16(void)
{
    const uint64_t in[5] = { 100U, 110U, 120U, 130U, 140U };
    run_round_trip_u64(&zserio_delta_traits_varuint16, in, 5U, 256U);
}

static void test_delta_traits_round_trip_varuint32(void)
{
    const uint64_t in[4] = { 1000000U, 1000010U, 1000005U, 999998U };
    run_round_trip_u64(&zserio_delta_traits_varuint32, in, 4U, 256U);
}

static void test_delta_traits_round_trip_varuint64(void)
{
    const uint64_t in[3] = { UINT64_C(0x0102030405), UINT64_C(0x0102030406),
                             UINT64_C(0x0102030400) };
    run_round_trip_u64(&zserio_delta_traits_varuint64, in, 3U, 256U);
}

static void test_delta_traits_round_trip_varuint(void)
{
    const uint64_t in[3] = { 7U, 17U, 27U };
    run_round_trip_u64(&zserio_delta_traits_varuint, in, 3U, 256U);
}

static void test_delta_traits_round_trip_varsize(void)
{
    const uint64_t in[4] = { 1U, 2U, 3U, 4U };
    run_round_trip_u64(&zserio_delta_traits_varsize, in, 4U, 256U);
}

/* Signed varint flavours. The wider negative-delta cases are covered by
 * the standalone test_delta_round_trip_signed_with_negative_deltas; here
 * we just walk through every signed flavour to lift trait-mode and bit-
 * size coverage. */

static void test_delta_traits_round_trip_varint16(void)
{
    const int16_t signed_in[4] = { 10, -5, 7, -3 };
    uint64_t in[4];
    for (size_t i = 0U; i < 4U; i++) {
        in[i] = (uint64_t)(int64_t)signed_in[i];
    }
    run_round_trip_u64(&zserio_delta_traits_varint16, in, 4U, 256U);
}

static void test_delta_traits_round_trip_varint32(void)
{
    const int32_t signed_in[4] = { 100000, -50000, 75000, -25000 };
    uint64_t in[4];
    for (size_t i = 0U; i < 4U; i++) {
        in[i] = (uint64_t)(int64_t)signed_in[i];
    }
    run_round_trip_u64(&zserio_delta_traits_varint32, in, 4U, 256U);
}

static void test_delta_traits_round_trip_varint64(void)
{
    const int64_t signed_in[3] = { INT64_C(1000000000), INT64_C(-500000000),
                                   INT64_C(750000000) };
    uint64_t in[3];
    for (size_t i = 0U; i < 3U; i++) {
        in[i] = (uint64_t)signed_in[i];
    }
    run_round_trip_u64(&zserio_delta_traits_varint64, in, 3U, 256U);
}

static void test_delta_traits_round_trip_varint(void)
{
    const int64_t signed_in[3] = { INT64_C(123), INT64_C(-456), INT64_C(789) };
    uint64_t in[3];
    for (size_t i = 0U; i < 3U; i++) {
        in[i] = (uint64_t)signed_in[i];
    }
    run_round_trip_u64(&zserio_delta_traits_varint, in, 3U, 256U);
}

/* Force the unpacked path through the trait-mode write/read callbacks:
 * widely-spaced values make the packed-delta cost exceed the unpacked
 * cost, so finalise clears the packed flag and every element goes
 * through write_unpacked / read_unpacked. */
static void test_delta_traits_unpacked_path(void)
{
    /* varuint32 deltas of ~2 million bytes force unpacked. */
    const uint64_t in[3] = { 1U, 2000000U, 1U };
    run_round_trip_u64(&zserio_delta_traits_varuint32, in, 3U, 256U);
}

/* ------------------------------------------------------------------------ */
/* zserio_delta_init_traits validation                                      */
/* ------------------------------------------------------------------------ */

static void test_delta_init_traits_invalid_args(void)
{
    /* Verifies: REQ-FS-006 */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));

    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_init_traits(NULL, &zserio_delta_traits_varuint16));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_init_traits(&ctx, NULL));

    /* Each NULL callback in the struct independently triggers the
     * validation reject. */
    zserio_delta_traits_t bad = zserio_delta_traits_varuint16;
    bad.read_unpacked = NULL;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_init_traits(&ctx, &bad));

    bad = zserio_delta_traits_varuint16;
    bad.write_unpacked = NULL;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_init_traits(&ctx, &bad));

    bad = zserio_delta_traits_varuint16;
    bad.bit_size_unpacked = NULL;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_delta_init_traits(&ctx, &bad));
}

static void test_delta_init_traits_idempotent(void)
{
    /* Re-init on an already-configured context returns OK without
     * resetting state — same contract as zserio_delta_init. */
    zserio_delta_context_t ctx;
    (void)memset(&ctx, 0, sizeof(ctx));
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_delta_init_traits(&ctx, &zserio_delta_traits_varuint16));
    TEST_ASSERT_EQUAL_PTR(&zserio_delta_traits_varuint16, ctx.traits);
    /* Second call on a configured context: still OK, traits unchanged. */
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_delta_init_traits(&ctx, &zserio_delta_traits_varint32));
    TEST_ASSERT_EQUAL_PTR(&zserio_delta_traits_varuint16, ctx.traits);
}

static void test_delta_init_traits_signedness_propagates(void)
{
    /* Signed traits set is_signed in the context; unsigned leave it false. */
    zserio_delta_context_t ctx_u;
    (void)memset(&ctx_u, 0, sizeof(ctx_u));
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_delta_init_traits(&ctx_u, &zserio_delta_traits_varuint16));
    TEST_ASSERT_FALSE(ctx_u.is_signed);

    zserio_delta_context_t ctx_s;
    (void)memset(&ctx_s, 0, sizeof(ctx_s));
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zserio_delta_init_traits(&ctx_s, &zserio_delta_traits_varint32));
    TEST_ASSERT_TRUE(ctx_s.is_signed);
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_delta_traits_round_trip_varuint16);
    RUN_TEST(test_delta_traits_round_trip_varuint32);
    RUN_TEST(test_delta_traits_round_trip_varuint64);
    RUN_TEST(test_delta_traits_round_trip_varuint);
    RUN_TEST(test_delta_traits_round_trip_varsize);
    RUN_TEST(test_delta_traits_round_trip_varint16);
    RUN_TEST(test_delta_traits_round_trip_varint32);
    RUN_TEST(test_delta_traits_round_trip_varint64);
    RUN_TEST(test_delta_traits_round_trip_varint);
    RUN_TEST(test_delta_traits_unpacked_path);
    RUN_TEST(test_delta_init_traits_invalid_args);
    RUN_TEST(test_delta_init_traits_idempotent);
    RUN_TEST(test_delta_init_traits_signedness_propagates);

    return UNITY_END();
}
