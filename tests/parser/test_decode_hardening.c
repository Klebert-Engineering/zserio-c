/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Decode-hardening emission tests. Each test drives the generator
 * over a small schema and asserts that the emitted decoder carries
 * the safety guards that protect the (untrusted) decode path:
 *
 *   - Wire-driven array allocations are bounded against both size_t
 *     multiply overflow and the bits left in the stream
 *     (REQ-FS-005 / REQ-FN-017 — bound resource use on malformed
 *     input). Packed arrays keep the overflow guard but drop the
 *     remaining-bits bound, whose "1 bit per element" assumption
 *     does not hold under delta packing.
 *   - Union tags read from the wire that name no declared arm are
 *     rejected, not silently accepted (REQ-FS-006).
 *   - Compound readers bound recursion via the reader depth counter
 *     so cyclic types cannot exhaust the C stack (REQ-FS-003).
 *
 * These are emission (source-text) assertions: they prove the
 * generator writes the guard. The runtime mechanisms the guards call
 * are covered behaviourally in tests/runtime/test_bitstream.c, and
 * that valid streams still round-trip is covered by the xvalidation
 * corpus.
 *
 * Verifies: REQ-FN-016 — encoder/decoder hardening on the read path.
 * Verifies: REQ-FS-003 — generated readers bound recursion.
 * Verifies: REQ-FS-006 — decoded discriminants validated at the boundary.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)    { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Sink that concatenates every emitted *.c into one NUL-terminated    */
/* blob, so a test can search the whole generated translation unit.    */
/* ------------------------------------------------------------------ */

typedef struct blob {
    char  *data;
    size_t len;
    size_t cap;
    bool   ok;
} blob_t;

static void blob_append(blob_t *bl, const char *bytes, size_t len)
{
    if (!bl->ok) return;
    if ((bl->len + len + 1U) > bl->cap) {
        size_t new_cap = (bl->cap == 0U) ? 4096U : bl->cap;
        while ((bl->len + len + 1U) > new_cap) new_cap *= 2U;
        char *const grown = (char *)realloc(bl->data, new_cap);
        if (grown == NULL) { bl->ok = false; return; }
        bl->data = grown;
        bl->cap  = new_cap;
    }
    (void)memcpy(&bl->data[bl->len], bytes, len);
    bl->len += len;
    bl->data[bl->len] = '\0';
}

static bool blob_write(void *ud, const char *path,
                       const char *bytes, size_t len)
{
    blob_t *const bl = (blob_t *)ud;
    const size_t pl = strlen(path);
    /* Only fold the .c translation units into the blob; headers carry
     * no executable guards. */
    if ((pl >= 2U) && (strcmp(&path[pl - 2U], ".c") == 0)) {
        blob_append(bl, bytes, len);
    }
    return bl->ok;
}

/* Parse → analyse → instantiate → emit `schema`; return the emitted
 * .c blob (caller frees). Asserts the front end produced no errors. */
static char *emit_c_blob(const char *schema)
{
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;
    zsp_sema_t           sema;
    blob_t               bl = { NULL, 0U, 0U, true };

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "test.zs", 7U, (const uint8_t *)schema, strlen(schema), &fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(&src, fid, &arena, &diag, &unit));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyse_unit(&sema, unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_instantiate_templates(&sema, unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_emit_unit(unit, &arena, &diag, &src,
                                              blob_write, &bl));
    TEST_ASSERT_TRUE_MESSAGE(bl.ok, "emit sink ran out of memory");
    TEST_ASSERT_NOT_NULL(bl.data);

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
    return bl.data;
}

static bool contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* An auto (length-prefixed) array read guards the allocation against
 * both size_t multiply overflow and a count larger than the bits left
 * in the stream, returning ARRAY_TOO_LARGE instead of corrupting the
 * heap / requesting a multi-GB allocation from a tiny input. */
static void test_auto_array_read_has_overflow_and_bomb_guards(void)
{
    char *const c = emit_c_blob(
            "package h;\n"
            "struct S { uint32 a[]; };\n");

    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "ZSERIO_ERR_ARRAY_TOO_LARGE"),
            "array read must reject oversized counts");
    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "SIZE_MAX / sizeof"),
            "array read must guard the size_t multiply against overflow");
    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "zserio_bitstream_reader_size(bs)"
                        " - zserio_bitstream_reader_position(bs)"),
            "array read must bound the count by the bits left in the stream");

    free(c);
}

/* A packed array keeps the overflow guard but omits the remaining-bits
 * bound, because delta packing can legitimately encode many elements
 * in fewer bits than there are elements. */
static void test_packed_array_keeps_overflow_drops_bomb_guard(void)
{
    char *const c = emit_c_blob(
            "package h;\n"
            "struct P { packed uint32 a[]; };\n");

    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "SIZE_MAX / sizeof"),
            "packed array read must still guard the multiply");
    TEST_ASSERT_FALSE_MESSAGE(
            contains(c, "zserio_bitstream_reader_size(bs)"
                        " - zserio_bitstream_reader_position(bs)"),
            "packed array read must not apply the 1-bit-per-element bound");

    free(c);
}

/* A union read rejects a wire tag that names no arm rather than
 * returning OK with an untouched union. */
static void test_union_read_rejects_invalid_tag(void)
{
    char *const c = emit_c_blob(
            "package h;\n"
            "union U { uint32 a; uint16 b; };\n");

    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "default: return ZSERIO_ERR_INVALID_TAG;"),
            "union read must reject an out-of-range tag");

    free(c);
}

/* Every compound reader brackets its body with the reader depth
 * guard so recursion is bounded on untrusted input. */
static void test_compound_read_brackets_depth_guard(void)
{
    char *const c = emit_c_blob(
            "package h;\n"
            "struct S { uint32 a; };\n");

    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "zserio_bitstream_reader_enter(bs)"),
            "compound read must enter the depth guard");
    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "zserio_bitstream_reader_leave(bs)"),
            "compound read must leave the depth guard on success");

    free(c);
}

/* A self-recursive struct (the canonical linked-list shape) still
 * routes its recursive read through the depth guard. */
static void test_recursive_struct_read_is_depth_guarded(void)
{
    char *const c = emit_c_blob(
            "package h;\n"
            "struct Node {\n"
            "    uint32 value;\n"
            "    optional Node next;\n"
            "};\n");

    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "zserio_bitstream_reader_enter(bs)"),
            "recursive struct read must be depth-guarded");
    /* Sanity: the schema really did produce a recursive pointer read. */
    TEST_ASSERT_TRUE_MESSAGE(
            contains(c, "next_present"),
            "expected the self-recursive optional field to be emitted");

    free(c);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_auto_array_read_has_overflow_and_bomb_guards);
    RUN_TEST(test_packed_array_keeps_overflow_drops_bomb_guard);
    RUN_TEST(test_union_read_rejects_invalid_tag);
    RUN_TEST(test_compound_read_brackets_depth_guard);
    RUN_TEST(test_recursive_struct_read_is_depth_guarded);
    return UNITY_END();
}
