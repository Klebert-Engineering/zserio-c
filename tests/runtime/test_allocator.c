/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

/*
 * Verifies: REQ-FS-001
 *
 * Exercise the libc-backed default allocator (`zserio_allocator_libc()`).
 * The other allocator-using tests (test_string, test_extern, test_bytes)
 * use a custom counting allocator; without this file, the libc backend
 * is uncovered.
 */

#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "zserio/allocator.h"

void setUp(void) {}
void tearDown(void) {}

static void test_allocator_libc_alloc_free(void)
{
    const zserio_allocator_t a = zserio_allocator_libc();
    TEST_ASSERT_NOT_NULL(a.vtable);
    TEST_ASSERT_NOT_NULL(a.vtable->alloc);
    TEST_ASSERT_NOT_NULL(a.vtable->free);

    void *p = a.vtable->alloc(a.userdata, 32U, 1U);
    TEST_ASSERT_NOT_NULL(p);
    /* Touch the memory to confirm it's writable. */
    (void)memset(p, 0xAB, 32U);
    a.vtable->free(a.userdata, p, 32U, 1U);
}

static void test_allocator_libc_realloc(void)
{
    const zserio_allocator_t a = zserio_allocator_libc();
    TEST_ASSERT_NOT_NULL(a.vtable->realloc);

    void *p = a.vtable->alloc(a.userdata, 16U, 1U);
    TEST_ASSERT_NOT_NULL(p);
    (void)memset(p, 0xCD, 16U);

    void *p2 = a.vtable->realloc(a.userdata, p, 16U, 64U, 1U);
    TEST_ASSERT_NOT_NULL(p2);
    /* The first 16 bytes preserve the old contents. */
    const uint8_t *bytes = (const uint8_t *)p2;
    for (size_t i = 0U; i < 16U; i++) {
        TEST_ASSERT_EQUAL_HEX8(0xCD, bytes[i]);
    }
    a.vtable->free(a.userdata, p2, 64U, 1U);
}

static void test_allocator_libc_free_null_is_noop(void)
{
    /* `free(NULL)` is the libc convention; verify our wrapper honours it. */
    const zserio_allocator_t a = zserio_allocator_libc();
    a.vtable->free(a.userdata, NULL, 0U, 1U);
}

/* ------------------------------------------------------------------------ */
/* Arena allocator                                                          */
/* ------------------------------------------------------------------------ */

static void test_arena_init_zeroes_state(void)
{
    uint8_t buf[64];
    zserio_arena_t a;
    /* Dirty the struct first so we can see init actually clears it. */
    (void)memset(&a, 0xFF, sizeof(a));
    zserio_arena_init(&a, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_PTR(buf, a.buf);
    TEST_ASSERT_EQUAL_UINT(sizeof(buf), a.cap);
    TEST_ASSERT_EQUAL_UINT(0U, a.used);
    TEST_ASSERT_EQUAL_UINT(0U, a.high_water);
}

static void test_arena_alloc_bumps_in_order(void)
{
    uint8_t buf[64];
    zserio_arena_t arena;
    zserio_arena_init(&arena, buf, sizeof(buf));
    const zserio_allocator_t a = zserio_allocator_arena(&arena);

    void *p1 = a.vtable->alloc(a.userdata, 16U, 1U);
    void *p2 = a.vtable->alloc(a.userdata, 8U, 1U);
    TEST_ASSERT_EQUAL_PTR(buf, p1);
    TEST_ASSERT_EQUAL_PTR(buf + 16, p2);
    TEST_ASSERT_EQUAL_UINT(24U, arena.used);
    TEST_ASSERT_EQUAL_UINT(24U, arena.high_water);
}

static void test_arena_alloc_aligns_up(void)
{
    uint8_t buf[64];
    zserio_arena_t arena;
    zserio_arena_init(&arena, buf, sizeof(buf));
    const zserio_allocator_t a = zserio_allocator_arena(&arena);

    /* Spend 3 bytes at alignment 1, then ask for 8 at alignment 8. The
     * arena must round up past the misaligned tail. */
    (void)a.vtable->alloc(a.userdata, 3U, 1U);
    void *p = a.vtable->alloc(a.userdata, 8U, 8U);
    TEST_ASSERT_EQUAL_PTR(buf + 8, p);
    TEST_ASSERT_EQUAL_UINT(16U, arena.used);
}

static void test_arena_alloc_exhausts_to_null(void)
{
    uint8_t buf[16];
    zserio_arena_t arena;
    zserio_arena_init(&arena, buf, sizeof(buf));
    const zserio_allocator_t a = zserio_allocator_arena(&arena);

    void *p1 = a.vtable->alloc(a.userdata, 12U, 1U);
    TEST_ASSERT_NOT_NULL(p1);
    /* 12 bytes used; only 4 left. */
    void *p2 = a.vtable->alloc(a.userdata, 8U, 1U);
    TEST_ASSERT_NULL(p2);
    /* Failing alloc must not bump `used`. */
    TEST_ASSERT_EQUAL_UINT(12U, arena.used);
}

static void test_arena_alloc_oom_guards_wrap(void)
{
    /* (aligned + size) must not silently wrap on a hostile (size,
     * alignment) pair. */
    uint8_t buf[8];
    zserio_arena_t arena;
    zserio_arena_init(&arena, buf, sizeof(buf));
    const zserio_allocator_t a = zserio_allocator_arena(&arena);

    void *p = a.vtable->alloc(a.userdata, (size_t)-1, 1U);
    TEST_ASSERT_NULL(p);
    TEST_ASSERT_EQUAL_UINT(0U, arena.used);
}

static void test_arena_reset_rewinds_but_keeps_high_water(void)
{
    uint8_t buf[64];
    zserio_arena_t arena;
    zserio_arena_init(&arena, buf, sizeof(buf));
    const zserio_allocator_t a = zserio_allocator_arena(&arena);

    (void)a.vtable->alloc(a.userdata, 40U, 1U);
    TEST_ASSERT_EQUAL_UINT(40U, arena.high_water);
    zserio_arena_reset(&arena);
    TEST_ASSERT_EQUAL_UINT(0U, arena.used);
    TEST_ASSERT_EQUAL_UINT(40U, arena.high_water); /* preserved */

    /* After reset the next alloc serves from the start of the buffer. */
    void *p = a.vtable->alloc(a.userdata, 8U, 1U);
    TEST_ASSERT_EQUAL_PTR(buf, p);
}

static void test_arena_free_is_noop(void)
{
    uint8_t buf[32];
    zserio_arena_t arena;
    zserio_arena_init(&arena, buf, sizeof(buf));
    const zserio_allocator_t a = zserio_allocator_arena(&arena);

    void *p = a.vtable->alloc(a.userdata, 16U, 1U);
    TEST_ASSERT_NOT_NULL(p);
    const size_t before = arena.used;
    a.vtable->free(a.userdata, p, 16U, 1U);
    /* free MUST NOT move `used` back — the arena reclaims only by reset. */
    TEST_ASSERT_EQUAL_UINT(before, arena.used);
}

static void test_arena_null_args_dont_crash(void)
{
    /* Init / reset must tolerate a NULL arena pointer. */
    zserio_arena_init(NULL, NULL, 0U);
    zserio_arena_reset(NULL);
    /* Allocator built around a zero-cap buffer hands out NULL for any
     * non-zero size. */
    uint8_t tmp[1];
    zserio_arena_t empty;
    zserio_arena_init(&empty, tmp, 0U);
    const zserio_allocator_t a = zserio_allocator_arena(&empty);
    TEST_ASSERT_NULL(a.vtable->alloc(a.userdata, 1U, 1U));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_allocator_libc_alloc_free);
    RUN_TEST(test_allocator_libc_realloc);
    RUN_TEST(test_allocator_libc_free_null_is_noop);
    RUN_TEST(test_arena_init_zeroes_state);
    RUN_TEST(test_arena_alloc_bumps_in_order);
    RUN_TEST(test_arena_alloc_aligns_up);
    RUN_TEST(test_arena_alloc_exhausts_to_null);
    RUN_TEST(test_arena_alloc_oom_guards_wrap);
    RUN_TEST(test_arena_reset_rewinds_but_keeps_high_water);
    RUN_TEST(test_arena_free_is_noop);
    RUN_TEST(test_arena_null_args_dont_crash);
    return UNITY_END();
}
