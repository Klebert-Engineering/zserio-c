/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/status.h"
#include "zserio/string.h"

/* ------------------------------------------------------------------------ */
/* Tracking allocator for tests                                             */
/* ------------------------------------------------------------------------ */

typedef struct {
    size_t alloc_count;
    size_t free_count;
    int    fail_alloc; /* if non-zero, the next alloc returns NULL */
} test_alloc_state_t;

static void *t_alloc(void *ud, size_t size, size_t align)
{
    (void)align;
    test_alloc_state_t *s = (test_alloc_state_t *)ud;
    if (s->fail_alloc != 0) {
        s->fail_alloc = 0;
        return NULL;
    }
    void *p = malloc(size);
    if (p != NULL) {
        s->alloc_count++;
    }
    return p;
}

static void t_free(void *ud, void *ptr, size_t size, size_t align)
{
    (void)size;
    (void)align;
    if (ptr != NULL) {
        test_alloc_state_t *s = (test_alloc_state_t *)ud;
        s->free_count++;
        free(ptr);
    }
}

static const zserio_allocator_vtable_t TEST_VTABLE = { t_alloc, NULL, t_free };

static test_alloc_state_t g_alloc_state;

static zserio_allocator_t test_allocator(void)
{
    g_alloc_state.alloc_count = 0U;
    g_alloc_state.free_count = 0U;
    g_alloc_state.fail_alloc = 0;
    const zserio_allocator_t a = { &TEST_VTABLE, &g_alloc_state };
    return a;
}

void setUp(void)
{
    g_alloc_state.alloc_count = 0U;
    g_alloc_state.free_count = 0U;
    g_alloc_state.fail_alloc = 0;
}
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* view_from_cstr                                                           */
/* ------------------------------------------------------------------------ */

static void test_view_from_cstr_null(void)
{
    const zserio_string_view_t v = zserio_string_view_from_cstr(NULL);
    TEST_ASSERT_NULL(v.bytes);
    TEST_ASSERT_EQUAL_size_t(0U, v.length);
}

static void test_view_from_cstr_literal(void)
{
    const zserio_string_view_t v = zserio_string_view_from_cstr("Zserio");
    TEST_ASSERT_NOT_NULL(v.bytes);
    TEST_ASSERT_EQUAL_size_t(6U, v.length);
}

/* ------------------------------------------------------------------------ */
/* Wire format — Encoding Guide example                                     */
/* ------------------------------------------------------------------------ */

static void test_write_string_encoding_guide_example(void)
{
    /* Verifies: REQ-FN-001
     *
     * Encoding Guide example: "Zserio is cool" encodes as
     *   0e 5a 73 65 72 69 6f 20 69 73 20 63 6f 6f 6c
     * (varsize length 14 = 0x0e, followed by the UTF-8 bytes).
     */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);

    const zserio_string_view_t v = zserio_string_view_from_cstr("Zserio is cool");
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_string(&w, v));

    const uint8_t expected[15] = {
        0x0e, 0x5a, 0x73, 0x65, 0x72, 0x69, 0x6f, 0x20,
        0x69, 0x73, 0x20, 0x63, 0x6f, 0x6f, 0x6c
    };
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expected, buf, 15U);
    TEST_ASSERT_EQUAL_size_t(120U, zserio_bitstream_writer_position(&w));
}

/* ------------------------------------------------------------------------ */
/* Round-trip via view                                                      */
/* ------------------------------------------------------------------------ */

static void test_string_round_trip_view_aligned(void)
{
    /* Verifies: REQ-FN-001 — view path with byte-aligned start. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    const zserio_string_view_t in = zserio_string_view_from_cstr("hello");
    (void)zserio_write_string(&w, in);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    zserio_string_view_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_string_view(&r, &out));
    TEST_ASSERT_EQUAL_size_t(5U, out.length);
    TEST_ASSERT_EQUAL_MEMORY("hello", out.bytes, 5U);
}

static void test_string_round_trip_view_empty(void)
{
    /* Empty string: varsize 0, no bytes. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    const zserio_string_view_t in = zserio_string_view_from_cstr("");
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_string(&w, in));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[0]);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    zserio_string_view_t out = { (const char *)0xDEADBEEF, 99U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_string_view(&r, &out));
    TEST_ASSERT_EQUAL_size_t(0U, out.length);
}

static void test_string_view_misaligned_fails(void)
{
    /* If the cursor is not byte-aligned at entry, view returns
     * INVALID_ARGUMENT and does not advance the cursor. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    /* Write a 1-bit then a string. */
    (void)zserio_bitstream_write_bits(&w, 1U, 1U);
    const zserio_string_view_t in = zserio_string_view_from_cstr("abc");
    (void)zserio_write_string(&w, in);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    /* Skip the 1 bit. Now at bit 1 (misaligned). */
    uint64_t skip = 0U;
    (void)zserio_bitstream_read_bits(&r, 1U, &skip);

    zserio_string_view_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string_view(&r, &out));
    TEST_ASSERT_EQUAL_size_t(1U, zserio_bitstream_reader_position(&r));
}

/* ------------------------------------------------------------------------ */
/* Round-trip via materializing read                                        */
/* ------------------------------------------------------------------------ */

static void test_string_round_trip_owned_aligned(void)
{
    /* Verifies: REQ-FS-001 — allocator vtable used; alloc/free balance. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    const zserio_string_view_t in = zserio_string_view_from_cstr("zserio");
    (void)zserio_write_string(&w, in);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_string(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(6U, out.length);
    TEST_ASSERT_EQUAL_MEMORY("zserio", out.bytes, 6U);
    TEST_ASSERT_EQUAL_size_t(1U, g_alloc_state.alloc_count);

    zserio_string_free(&out, &alloc);
    TEST_ASSERT_NULL(out.bytes);
    TEST_ASSERT_EQUAL_size_t(0U, out.length);
    TEST_ASSERT_EQUAL_size_t(1U, g_alloc_state.free_count);
}

static void test_string_round_trip_owned_large(void)
{
    /* Verifies: REQ-FN-001 — exercises the memcpy fast path in
     * zserio_read_string. A 20-byte string exceeds the 8-byte
     * bit-cache capacity, so the read drains the cache then
     * `memcpy`s the remainder from the source buffer. */
    const char *literal = "This is twenty bytes";
    uint8_t buf[40] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U * sizeof(buf));
    const zserio_string_view_t in = zserio_string_view_from_cstr(literal);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_write_string(&w, in));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_string(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(20U, out.length);
    TEST_ASSERT_EQUAL_MEMORY(literal, out.bytes, 20U);

    zserio_string_free(&out, &alloc);
}

static void test_string_round_trip_owned_misaligned(void)
{
    /* Verifies: REQ-FN-001 — materializing read works for misaligned start. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    /* 3 bits then a string → string starts mid-byte. */
    (void)zserio_bitstream_write_bits(&w, 3U, 0x05U);
    const zserio_string_view_t in = zserio_string_view_from_cstr("ABC");
    (void)zserio_write_string(&w, in);

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    uint64_t skip = 0U;
    (void)zserio_bitstream_read_bits(&r, 3U, &skip);

    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_string(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(3U, out.length);
    TEST_ASSERT_EQUAL_MEMORY("ABC", out.bytes, 3U);
    zserio_string_free(&out, &alloc);
    TEST_ASSERT_EQUAL_size_t(g_alloc_state.alloc_count, g_alloc_state.free_count);
}

static void test_string_owned_empty_no_allocation(void)
{
    /* Empty string read should not allocate. */
    uint8_t buf[1] = { 0x00 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);

    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_read_string(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, out.length);
    TEST_ASSERT_NULL(out.bytes);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc_state.alloc_count);
}

/* ------------------------------------------------------------------------ */
/* Allocator failure handling                                               */
/* ------------------------------------------------------------------------ */

static void test_string_alloc_failure_returns_oom(void)
{
    /* Verifies: REQ-FS-001, REQ-FS-006 — alloc failure surfaces as
     * ZSERIO_ERR_OUT_OF_MEMORY without leaking. */
    uint8_t buf[16] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 128U);
    (void)zserio_write_string(&w, zserio_string_view_from_cstr("hello"));

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    zserio_allocator_t alloc = test_allocator();
    g_alloc_state.fail_alloc = 1;

    zserio_string_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_OUT_OF_MEMORY,
                      zserio_read_string(&r, &alloc, &out));
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc_state.alloc_count);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc_state.free_count);
}

/* ------------------------------------------------------------------------ */
/* Defensive checks                                                         */
/* ------------------------------------------------------------------------ */

static void test_write_string_too_long(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    /* Constructed view with bogus over-cap length; bytes pointer doesn't
     * matter for the length check. */
    zserio_string_view_t v;
    v.bytes = (const char *)0xDEADBEEF;
    v.length = (size_t)0x80000000U; /* 2^31 — one over the cap. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_VALUE_OUT_OF_RANGE,
                      zserio_write_string(&w, v));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_writer_position(&w));
}

static void test_write_string_null_bytes_with_length(void)
{
    /* Verifies: REQ-FS-006 */
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    zserio_string_view_t v;
    v.bytes = NULL;
    v.length = 5U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_write_string(&w, v));
}

static void test_read_string_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t out;

    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string(NULL, &alloc, &out));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string(&r, &alloc, NULL));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string(&r, NULL, &out));

    /* Allocator with NULL vtable should be rejected. */
    zserio_allocator_t bad = { NULL, NULL };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string(&r, &bad, &out));
}

static void test_read_string_view_null_args(void)
{
    /* Verifies: REQ-FS-006 */
    const uint8_t buf[1] = { 0 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    zserio_string_view_t out;

    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string_view(NULL, &out));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_INVALID_ARGUMENT,
                      zserio_read_string_view(&r, NULL));
}

static void test_read_string_truncated_buffer_no_leak(void)
{
    /* If the buffer ends mid-string, the materializing read returns
     * END_OF_STREAM without leaking memory through the allocator. */
    /* Build a buffer with varsize=10 but only 5 string bytes available. */
    const uint8_t buf[6] = { 0x0A, 0x41, 0x42, 0x43, 0x44, 0x45 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 48U);

    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_read_string(&r, &alloc, &out));
    /* No allocation must remain held: alloc_count == free_count (both may
     * be zero, since we bound-check before allocating). */
    TEST_ASSERT_EQUAL_size_t(g_alloc_state.alloc_count, g_alloc_state.free_count);
}

static void test_string_free_on_zero_struct(void)
{
    /* Calling free on a zero-initialised struct must be a no-op. */
    const zserio_allocator_t alloc = test_allocator();
    zserio_string_t s = { NULL, 0U };
    zserio_string_free(&s, &alloc);
    TEST_ASSERT_NULL(s.bytes);
    TEST_ASSERT_EQUAL_size_t(0U, s.length);
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc_state.free_count);
    /* And on NULL self too. */
    zserio_string_free(NULL, &alloc);
}

static void test_string_view_truncated_buffer(void)
{
    /* varsize claims length=10 but buffer holds fewer bytes. */
    const uint8_t buf[6] = { 0x0A, 0x41, 0x42, 0x43, 0x44, 0x45 };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 48U);
    zserio_string_view_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_read_string_view(&r, &out));
    TEST_ASSERT_EQUAL_size_t(0U, zserio_bitstream_reader_position(&r));
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_view_from_cstr_null);
    RUN_TEST(test_view_from_cstr_literal);

    RUN_TEST(test_write_string_encoding_guide_example);
    RUN_TEST(test_string_round_trip_view_aligned);
    RUN_TEST(test_string_round_trip_view_empty);
    RUN_TEST(test_string_view_misaligned_fails);

    RUN_TEST(test_string_round_trip_owned_aligned);
    RUN_TEST(test_string_round_trip_owned_large);
    RUN_TEST(test_string_round_trip_owned_misaligned);
    RUN_TEST(test_string_owned_empty_no_allocation);

    RUN_TEST(test_string_alloc_failure_returns_oom);

    RUN_TEST(test_write_string_too_long);
    RUN_TEST(test_write_string_null_bytes_with_length);
    RUN_TEST(test_read_string_null_args);
    RUN_TEST(test_read_string_view_null_args);
    RUN_TEST(test_read_string_truncated_buffer_no_leak);
    RUN_TEST(test_string_free_on_zero_struct);
    RUN_TEST(test_string_view_truncated_buffer);

    return UNITY_END();
}
