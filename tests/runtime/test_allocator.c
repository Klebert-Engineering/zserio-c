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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_allocator_libc_alloc_free);
    RUN_TEST(test_allocator_libc_realloc);
    RUN_TEST(test_allocator_libc_free_null_is_noop);
    return UNITY_END();
}
