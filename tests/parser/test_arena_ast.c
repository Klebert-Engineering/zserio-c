/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Unit tests for the parser arena and AST node constructors.
 */

#include <string.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"

static zserio_allocator_t libc_alloc;

void setUp(void)
{
    libc_alloc = zserio_allocator_libc();
}

void tearDown(void) {}

static void test_arena_basic_alloc(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    uint32_t *const p = (uint32_t *)zsp_arena_alloc(&a, sizeof(uint32_t),
                                                    sizeof(uint32_t));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT32(0U, *p);   /* zero-initialised */
    *p = 0xDEADBEEF;
    TEST_ASSERT_EQUAL_UINT32(0xDEADBEEF, *p);

    zsp_arena_dispose(&a);
}

static void test_arena_alignment(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    (void)zsp_arena_alloc(&a, 1U, 1U);                   /* unaligned start */
    uint64_t *const p = (uint64_t *)zsp_arena_alloc(&a, sizeof(uint64_t),
                                                    sizeof(uint64_t));
    TEST_ASSERT_NOT_NULL(p);
    TEST_ASSERT_EQUAL_UINT64(0U,
            (uint64_t)((uintptr_t)p & (sizeof(uint64_t) - 1U)));

    zsp_arena_dispose(&a);
}

static void test_arena_grows_to_new_chunks(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    /* Fill more than one default chunk worth so growth kicks in. */
    const size_t per = 1024U;
    const size_t count = (ZSP_ARENA_CHUNK_BYTES / per) + 4U;
    for (size_t i = 0U; i < count; ++i) {
        void *const p = zsp_arena_alloc(&a, per, sizeof(void *));
        TEST_ASSERT_NOT_NULL(p);
    }
    TEST_ASSERT_TRUE(a.bytes_total > ZSP_ARENA_CHUNK_BYTES);

    zsp_arena_dispose(&a);
}

static void test_arena_oversize_allocation(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    /* Allocation larger than ZSP_ARENA_CHUNK_BYTES should still
     * succeed via a dedicated chunk. */
    const size_t big = ZSP_ARENA_CHUNK_BYTES * 2U;
    void *const p = zsp_arena_alloc(&a, big, sizeof(void *));
    TEST_ASSERT_NOT_NULL(p);

    zsp_arena_dispose(&a);
}

static void test_arena_strdup(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    char *const s = zsp_arena_strdup(&a, "hello world", 11U);
    TEST_ASSERT_NOT_NULL(s);
    TEST_ASSERT_EQUAL_STRING("hello world", s);

    char *const empty = zsp_arena_strdup(&a, "", 0U);
    TEST_ASSERT_NOT_NULL(empty);
    TEST_ASSERT_EQUAL_STRING("", empty);

    zsp_arena_dispose(&a);
}

static void test_ast_new_basic(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    zsp_ast_node_t *const n = zsp_ast_new(&a, ZSP_AST_EXPR_INT_LIT,
                                          (zsp_file_id_t)0, 42U, 4U);
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_INT(ZSP_AST_EXPR_INT_LIT, n->kind);
    TEST_ASSERT_EQUAL_UINT32(42U, n->offset);
    TEST_ASSERT_EQUAL_UINT32(4U,  n->length);
    n->u.int_lit.value = 1234ULL;
    TEST_ASSERT_EQUAL_UINT64(1234ULL, n->u.int_lit.value);

    zsp_arena_dispose(&a);
}

static void test_ast_list_grows_and_takes(void)
{
    zsp_arena_t a;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&a, &libc_alloc));

    zsp_ast_list_t list;
    zsp_ast_list_init(&list, &a);

    /* Push 13 nodes — exercises the doubling growth (4 -> 8 -> 16). */
    for (size_t i = 0U; i < 13U; ++i) {
        zsp_ast_node_t *const n = zsp_ast_new(&a, ZSP_AST_EXPR_IDENT,
                                              (zsp_file_id_t)0,
                                              (uint32_t)i, 1U);
        TEST_ASSERT_NOT_NULL(n);
        TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_ast_list_push(&list, n));
    }
    TEST_ASSERT_EQUAL_size_t(13U, list.count);

    zsp_ast_node_t **items = NULL;
    size_t           count = 0U;
    zsp_ast_list_take(&list, &items, &count);
    TEST_ASSERT_EQUAL_size_t(13U, count);
    TEST_ASSERT_NOT_NULL(items);
    for (size_t i = 0U; i < count; ++i) {
        TEST_ASSERT_EQUAL_UINT32((uint32_t)i, items[i]->offset);
    }

    zsp_arena_dispose(&a);
}

static void test_ast_kind_name(void)
{
    TEST_ASSERT_EQUAL_STRING("struct",   zsp_ast_kind_name(ZSP_AST_STRUCT));
    TEST_ASSERT_EQUAL_STRING("binop",    zsp_ast_kind_name(ZSP_AST_EXPR_BINARY));
    TEST_ASSERT_EQUAL_STRING("type-array", zsp_ast_kind_name(ZSP_AST_TYPE_ARRAY));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_arena_basic_alloc);
    RUN_TEST(test_arena_alignment);
    RUN_TEST(test_arena_grows_to_new_chunks);
    RUN_TEST(test_arena_oversize_allocation);
    RUN_TEST(test_arena_strdup);
    RUN_TEST(test_ast_new_basic);
    RUN_TEST(test_ast_list_grows_and_takes);
    RUN_TEST(test_ast_kind_name);
    return UNITY_END();
}
