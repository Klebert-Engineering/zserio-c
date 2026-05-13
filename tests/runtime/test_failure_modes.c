/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Failure-mode injection tests.
 *
 * One test per actionable HARA row in
 * docs/safety-case/hazard-analysis.md. Each test exercises the
 * documented mitigation path for one hazard end-to-end:
 *   - construct a stimulus that would trigger the hazard;
 *   - drive it through the public API;
 *   - assert the runtime returns the documented status code rather
 *     than crashing, looping, or producing wrong output.
 *
 * Hazards covered here:
 *   H-01 buffer over/underrun        — reader/writer end-of-buffer
 *   H-02 unbounded execution         — malformed-varint encoding
 *   H-03 allocator misbehavior       — failing allocator
 *   H-06 freedom from interference   — two independent codec instances
 *
 * Hazards not covered here (handled elsewhere; recorded for the
 * traceability audit):
 *   H-04 undetected payload corruption — out of scope at item level
 *   H-05 schema/decoder version skew   — out of scope at item level
 *   H-07 reflection / dynamic dispatch — REQ-FS-008 enforced statically by run_misra.sh
 *   H-08 wire-format divergence        — tests/xvalidation/ (CI-gated)
 *   H-09 stack overflow                — tools/measure_stack_usage.py (CI-gated)
 *   H-10 toolchain miscompilation      — out of scope at item level
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"
#include "zserio/allocator.h"
#include "zserio/bitstream.h"
#include "zserio/bytes.h"
#include "zserio/status.h"
#include "zserio/varint.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------------ */
/* Tracking allocator with fault injection                                  */
/* ------------------------------------------------------------------------ */

typedef struct {
    size_t alloc_count;
    size_t free_count;
    int    fail_alloc;
} alloc_state_t;

static void *t_alloc(void *ud, size_t size, size_t align)
{
    (void)align;
    alloc_state_t *s = (alloc_state_t *)ud;
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
        ((alloc_state_t *)ud)->free_count++;
        free(ptr);
    }
}

static const zserio_allocator_vtable_t TRACK_VTABLE = { t_alloc, NULL, t_free };
static alloc_state_t g_alloc;

static zserio_allocator_t tracking_allocator(void)
{
    g_alloc.alloc_count = 0U;
    g_alloc.free_count = 0U;
    g_alloc.fail_alloc = 0;
    return (zserio_allocator_t){ &TRACK_VTABLE, &g_alloc };
}

/* ------------------------------------------------------------------------ */
/* H-01 — buffer over/underrun                                              */
/* ------------------------------------------------------------------------ */

static void test_h01_read_past_end_returns_eof(void)
{
    /* Verifies: REQ-FS-006 — read past end of a constructed bitstream
     * returns ZSERIO_ERR_END_OF_STREAM rather than reading garbage
     * past the buffer or crashing. HARA H-01 mitigation. */
    const uint8_t buf[1] = { 0xAA };
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    uint64_t v = 0U;
    /* Consume the only valid byte. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&r, 8U, &v));
    TEST_ASSERT_EQUAL_HEX64(0xAAU, v);
    /* Any further read of any width must fail. */
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_bitstream_read_bits(&r, 1U, &v));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_bitstream_read_bits(&r, 8U, &v));
}

static void test_h01_write_past_end_returns_overflow(void)
{
    /* Verifies: REQ-FS-006 — write past buffer capacity returns
     * ZSERIO_ERR_BUFFER_OVERFLOW rather than corrupting adjacent
     * memory. HARA H-01 mitigation. */
    uint8_t buf[1] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 8U);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&w, 8U, 0xFFU));
    TEST_ASSERT_EQUAL(ZSERIO_ERR_BUFFER_OVERFLOW,
                      zserio_bitstream_write_bits(&w, 1U, 0U));
}

/* ------------------------------------------------------------------------ */
/* H-02 — unbounded execution on malformed input                            */
/* ------------------------------------------------------------------------ */

static void test_h02_varuint16_decoder_is_bounded_by_max_bytes(void)
{
    /* Verifies: REQ-FS-005, REQ-FS-006 — the varuint16 decoder
     * consumes at most max_bytes (= 2) regardless of input. The
     * wire-format property guaranteeing this is that the final
     * byte position carries no continuation bit by definition;
     * hostile "all continuation" inputs cannot make the decoder
     * read more bytes than the type's max. HARA H-02 mitigation:
     * loop bound is structural, not data-dependent.
     *
     * Construct a buffer much larger than the type's max bytes
     * and prove the reader cursor advances by at most max_bytes
     * worth of bits, irrespective of what's in the buffer. */
    uint8_t buf[64];
    (void)memset(buf, 0x80U, sizeof(buf)); /* continuation everywhere */
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, sizeof(buf) * 8U);
    uint16_t v = 0U;
    const zserio_status_t st = zserio_read_varuint16(&r, &v);
    /* Either succeeds (valid encoding consumed) or returns a
     * defined error. Either way the cursor must not have advanced
     * past max_bytes = 2 bytes = 16 bits. */
    TEST_ASSERT_TRUE(st == ZSERIO_OK
                  || st == ZSERIO_ERR_INVALID_ENCODING
                  || st == ZSERIO_ERR_END_OF_STREAM);
    /* The structural bound: at most 16 bits consumed for a varuint16. */
    TEST_ASSERT_TRUE(r.bit_index <= 16U);
}

static void test_h02_truncated_varuint_returns_eof(void)
{
    /* Verifies: REQ-FS-006 — a varint that signals "more bytes
     * follow" but the buffer ends first returns END_OF_STREAM. The
     * read is still bounded by the buffer, not the wire content. */
    const uint8_t buf[1] = { 0x80U }; /* continuation set, no follow-up */
    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf, 8U);
    uint16_t v = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_ERR_END_OF_STREAM,
                      zserio_read_varuint16(&r, &v));
}

/* ------------------------------------------------------------------------ */
/* H-03 — allocator misbehavior under low-memory conditions                 */
/* ------------------------------------------------------------------------ */

static void test_h03_failing_allocator_propagates_out_of_memory(void)
{
    /* Verifies: REQ-FS-001 — when the allocator returns NULL the
     * runtime returns ZSERIO_ERR_OUT_OF_MEMORY rather than dereferencing
     * the NULL pointer. HARA H-03 mitigation. The bytes codec is the
     * representative path: it allocates a buffer to receive the read
     * payload, so failure there exercises the full allocation site. */
    const uint8_t in[2] = { 0xABU, 0xCDU };
    uint8_t buf[8] = { 0 };
    zserio_bitstream_writer_t w;
    (void)zserio_bitstream_writer_init(&w, buf, 64U);
    (void)zserio_write_bytes(&w, (zserio_bytes_view_t){ in, 2U });

    zserio_bitstream_reader_t r;
    (void)zserio_bitstream_reader_init(&r, buf,
                                       zserio_bitstream_writer_position(&w));
    zserio_allocator_t alloc = tracking_allocator();
    g_alloc.fail_alloc = 1;
    zserio_bytes_t out = { NULL, 0U };
    TEST_ASSERT_EQUAL(ZSERIO_ERR_OUT_OF_MEMORY,
                      zserio_read_bytes(&r, &alloc, &out));
    /* No allocator leak: the failed alloc never counted as a successful
     * allocation, so free count must be zero. */
    TEST_ASSERT_EQUAL_size_t(0U, g_alloc.free_count);
}

/* ------------------------------------------------------------------------ */
/* H-06 — freedom from interference between codec instances                 */
/* ------------------------------------------------------------------------ */

static void test_h06_two_readers_disjoint_state(void)
{
    /* Verifies: REQ-FS-007 — two readers initialised on different
     * buffers advance independently. Reading from one does not
     * affect the other's bit position or return values. HARA H-06
     * mitigation. */
    const uint8_t buf_a[2] = { 0x11U, 0x22U };
    const uint8_t buf_b[2] = { 0xAAU, 0xBBU };
    zserio_bitstream_reader_t ra;
    zserio_bitstream_reader_t rb;
    (void)zserio_bitstream_reader_init(&ra, buf_a, 16U);
    (void)zserio_bitstream_reader_init(&rb, buf_b, 16U);

    uint64_t va = 0U;
    uint64_t vb = 0U;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&ra, 8U, &va));
    TEST_ASSERT_EQUAL_HEX64(0x11U, va);
    /* Reading from ra must not have advanced rb. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&rb, 8U, &vb));
    TEST_ASSERT_EQUAL_HEX64(0xAAU, vb);
    /* And ra's next byte is unaffected by rb's read. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&ra, 8U, &va));
    TEST_ASSERT_EQUAL_HEX64(0x22U, va);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_read_bits(&rb, 8U, &vb));
    TEST_ASSERT_EQUAL_HEX64(0xBBU, vb);
}

static void test_h06_two_writers_disjoint_state(void)
{
    /* Verifies: REQ-FS-007 — same property for the writer side. The
     * runtime has no static buffers; each writer carries its own
     * cursor. */
    uint8_t buf_a[2] = { 0 };
    uint8_t buf_b[2] = { 0 };
    zserio_bitstream_writer_t wa;
    zserio_bitstream_writer_t wb;
    (void)zserio_bitstream_writer_init(&wa, buf_a, 16U);
    (void)zserio_bitstream_writer_init(&wb, buf_b, 16U);

    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&wa, 8U, 0x11U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&wb, 8U, 0xAAU));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&wa, 8U, 0x22U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zserio_bitstream_write_bits(&wb, 8U, 0xBBU));

    TEST_ASSERT_EQUAL_HEX8(0x11U, buf_a[0]);
    TEST_ASSERT_EQUAL_HEX8(0x22U, buf_a[1]);
    TEST_ASSERT_EQUAL_HEX8(0xAAU, buf_b[0]);
    TEST_ASSERT_EQUAL_HEX8(0xBBU, buf_b[1]);
}

/* ------------------------------------------------------------------------ */
/* Test runner                                                              */
/* ------------------------------------------------------------------------ */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_h01_read_past_end_returns_eof);
    RUN_TEST(test_h01_write_past_end_returns_overflow);

    RUN_TEST(test_h02_varuint16_decoder_is_bounded_by_max_bytes);
    RUN_TEST(test_h02_truncated_varuint_returns_eof);

    RUN_TEST(test_h03_failing_allocator_propagates_out_of_memory);

    RUN_TEST(test_h06_two_readers_disjoint_state);
    RUN_TEST(test_h06_two_writers_disjoint_state);

    return UNITY_END();
}
