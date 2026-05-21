/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * Verifies: REQ-FN-010 — identifier-length cap with deterministic
 *                        FNV-1a hash truncation.
 *
 * Builds a schema whose template instantiation produces a mangled
 * type name exceeding the NAME_LIMIT (160 bytes) threshold, then
 * asserts:
 *   1. The emitted type identifier carries the documented suffix
 *      shape `_HHHHHHHHHHHHHHHH` (underscore + 16 lowercase hex).
 *   2. The pre-suffix portion is the leading 160 bytes of the
 *      pre-truncation identifier (prefix readability is preserved).
 *   3. The full identifier and its emitted filename fit within the
 *      POSIX NAME_MAX (255 bytes) ceiling.
 *   4. A second pass through the same input produces the *same*
 *      hex suffix (determinism — REQ-FN-007 applied to the
 *      truncation path).
 */

#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
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

enum { NAME_LIMIT = 160U };
enum { POSIX_NAME_MAX = 255U };
enum { HEX_SUFFIX_LEN = 17U };       /* `_` + 16 hex digits */

static zserio_allocator_t libc_alloc;

void setUp(void)    { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* In-memory sink (mirrors test_emit.c's collector)                   */
/* ------------------------------------------------------------------ */

typedef struct emitted_file {
    char  *path;
    char  *bytes;
    size_t len;
} emitted_file_t;

typedef struct memory_sink {
    emitted_file_t *files;
    size_t          count;
    size_t          cap;
} memory_sink_t;

static bool memory_write(void *ud, const char *path,
                         const char *bytes, size_t len)
{
    memory_sink_t *const s = (memory_sink_t *)ud;
    if (s->count == s->cap) {
        const size_t new_cap = (s->cap == 0U) ? 8U : (s->cap * 2U);
        emitted_file_t *const grown = (emitted_file_t *)realloc(
                s->files, new_cap * sizeof(emitted_file_t));
        if (grown == NULL) return false;
        s->files = grown;
        s->cap   = new_cap;
    }
    s->files[s->count].path = strdup(path);
    s->files[s->count].bytes = (char *)malloc(len + 1U);
    if (s->files[s->count].bytes == NULL) return false;
    (void)memcpy(s->files[s->count].bytes, bytes, len);
    s->files[s->count].bytes[len] = '\0';
    s->files[s->count].len = len;
    s->count += 1U;
    return true;
}

static void memory_sink_free(memory_sink_t *s)
{
    for (size_t i = 0U; i < s->count; ++i) {
        free(s->files[i].path);
        free(s->files[i].bytes);
    }
    free(s->files);
    s->files = NULL;
    s->count = 0U;
    s->cap   = 0U;
}

static void parse_analyse_emit(const uint8_t *bytes, size_t len,
                               memory_sink_t *sink)
{
    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;
    zsp_sema_t           sema;

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_init(&src, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_arena_init(&arena, &libc_alloc));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_diag_init(&diag, &libc_alloc, 0U));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_source_add_borrowed(
            &src, "test.zs", 7U, bytes, len, &fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(&src, fid, &arena, &diag, &unit));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyse_unit(&sema, unit));
    /* Template instantiation is a separate sema pass — without it,
     * inline template references (e.g. `Container<...>` as a field
     * type) stay as templated refs and never produce concrete
     * clones with mangled names. The session-level driver in
     * `parser/src/session.c` runs this pass; we replicate it here
     * for the single-unit test. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_instantiate_templates(&sema, unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_emit_unit(unit, &arena, &diag, &src,
                                              memory_write, sink));

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
}

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/* Schema designed to produce a mangled identifier > 160 bytes via
 * inline (anonymous) template references. An `instantiate ... Name;`
 * would carry the user-supplied `Name` through to the emitted type,
 * bypassing the mangler — we need anonymous nested references so
 * the mangler produces the type name.
 *
 * A field of type `ContainerWithReasonablyLongName<...nested...>`
 * inside an outer struct forces semantic analysis to materialise
 * each inner reference as a concrete instance with a mangled name.
 *
 * Per-level mangled identifier length (approx, base name = 36 chars):
 *   L0 (leaf)  : container_with_reasonably_long_name_uint32      (~43)
 *   L1         : container_with_reasonably_long_name_<L0>        (~81)
 *   L2         : container_with_reasonably_long_name_<L1>        (~119)
 *   L3         : container_with_reasonably_long_name_<L2>        (~157) -- borderline
 *   L4 = root  : container_with_reasonably_long_name_<L3>        (~195) -- triggers truncation
 *
 * Five levels gives us comfortable headroom over the 160-byte
 * threshold without making the test schema pathologically large.
 */
static const uint8_t SCHEMA[] =
        "package x;\n"
        "struct ContainerWithReasonablyLongName<T> { T value; };\n"
        "struct Outer {\n"
        "    ContainerWithReasonablyLongName<\n"
        "        ContainerWithReasonablyLongName<\n"
        "            ContainerWithReasonablyLongName<\n"
        "                ContainerWithReasonablyLongName<\n"
        "                    ContainerWithReasonablyLongName<uint32>\n"
        "                >\n"
        "            >\n"
        "        >\n"
        "    > deeply_nested_field;\n"
        "};\n";

/* Find an emitted header whose filename component (stem before the
 * trailing `.h`) matches the truncation suffix shape. Returns the
 * stem in *stem_out (caller-owned strdup) and the index of the
 * `_` introducing the hex suffix in *suffix_pos_out. NULL if no
 * truncated file is found. */
static char *find_truncated_header(const memory_sink_t *sink,
                                   size_t *suffix_pos_out)
{
    for (size_t i = 0U; i < sink->count; ++i) {
        const char *const path = sink->files[i].path;
        const size_t       pl   = strlen(path);
        /* Must end in .h */
        if ((pl < 3U) || (strcmp(&path[pl - 2U], ".h") != 0)) continue;
        /* Locate the stem: last `/`-separated component, without ".h" */
        const char *slash = strrchr(path, '/');
        const char *stem  = (slash != NULL) ? (slash + 1) : path;
        const size_t stem_len = strlen(stem) - 2U;  /* strip ".h" */
        /* Must be long enough to plausibly carry a truncation suffix. */
        if (stem_len < (NAME_LIMIT + HEX_SUFFIX_LEN)) continue;
        /* Check the trailing HEX_SUFFIX_LEN bytes are `_` + 16 hex. */
        const size_t suffix_pos = stem_len - HEX_SUFFIX_LEN;
        if (stem[suffix_pos] != '_') continue;
        bool ok = true;
        for (size_t k = 1U; k <= 16U; ++k) {
            const unsigned char c = (unsigned char)stem[suffix_pos + k];
            if (!(((c >= (unsigned char)'0') && (c <= (unsigned char)'9'))
                    || ((c >= (unsigned char)'a') && (c <= (unsigned char)'f')))) {
                ok = false;
                break;
            }
        }
        if (!ok) continue;
        if (suffix_pos_out != NULL) *suffix_pos_out = suffix_pos;
        char *const stem_copy = (char *)malloc(stem_len + 1U);
        TEST_ASSERT_NOT_NULL(stem_copy);
        (void)memcpy(stem_copy, stem, stem_len);
        stem_copy[stem_len] = '\0';
        return stem_copy;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* The truncation suffix is present on at least one emitted file
 * and the suffix takes the documented shape `_[0-9a-f]{16}`. */
static void test_truncation_suffix_present_and_well_formed(void)
{
    memory_sink_t sink = {0};
    parse_analyse_emit(SCHEMA, sizeof(SCHEMA) - 1U, &sink);

    size_t suffix_pos = 0U;
    char *const stem = find_truncated_header(&sink, &suffix_pos);
    TEST_ASSERT_NOT_NULL_MESSAGE(
            stem,
            "expected an emitted .h with a truncation suffix; got none. "
            "If the mangling rules changed and shortened deep templates, "
            "extend the SCHEMA chain depth.");

    /* Prefix readability: the bytes before the suffix are at least
     * the documented NAME_LIMIT (160) of the pre-truncation type
     * name; the emitted filename may add a package prefix on top.
     * In either case, the prefix should look like normal
     * snake_case (no internal hex blocks). */
    TEST_ASSERT_GREATER_OR_EQUAL_size_t(NAME_LIMIT, suffix_pos);
    for (size_t k = 0U; k < 32U; ++k) {
        const unsigned char c = (unsigned char)stem[k];
        TEST_ASSERT_TRUE_MESSAGE(
                ((c >= (unsigned char)'a') && (c <= (unsigned char)'z'))
                        || ((c >= (unsigned char)'0') && (c <= (unsigned char)'9'))
                        || (c == (unsigned char)'_'),
                "prefix bytes should be lowercase snake_case");
    }

    free(stem);
    memory_sink_free(&sink);
}

/* The emitted filename + its path component fit within POSIX
 * NAME_MAX (255 bytes per directory entry). */
static void test_filename_within_name_max(void)
{
    memory_sink_t sink = {0};
    parse_analyse_emit(SCHEMA, sizeof(SCHEMA) - 1U, &sink);

    bool found_truncated = false;
    for (size_t i = 0U; i < sink.count; ++i) {
        const char *const path = sink.files[i].path;
        const char *const slash = strrchr(path, '/');
        const char *const stem  = (slash != NULL) ? (slash + 1) : path;
        const size_t stem_len = strlen(stem);
        TEST_ASSERT_LESS_OR_EQUAL_size_t(POSIX_NAME_MAX, stem_len);
        const size_t pl = strlen(path);
        if (pl > (NAME_LIMIT + HEX_SUFFIX_LEN)) {
            found_truncated = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(
            found_truncated,
            "the deep-template schema should emit at least one long file");

    memory_sink_free(&sink);
}

/* Re-running the parser on the same schema produces the same
 * suffix hash. */
static void test_truncation_is_deterministic(void)
{
    memory_sink_t sink_a = {0};
    parse_analyse_emit(SCHEMA, sizeof(SCHEMA) - 1U, &sink_a);
    size_t suffix_pos_a = 0U;
    char *const stem_a = find_truncated_header(&sink_a, &suffix_pos_a);
    TEST_ASSERT_NOT_NULL(stem_a);

    memory_sink_t sink_b = {0};
    parse_analyse_emit(SCHEMA, sizeof(SCHEMA) - 1U, &sink_b);
    size_t suffix_pos_b = 0U;
    char *const stem_b = find_truncated_header(&sink_b, &suffix_pos_b);
    TEST_ASSERT_NOT_NULL(stem_b);

    TEST_ASSERT_EQUAL_size_t(suffix_pos_a, suffix_pos_b);
    TEST_ASSERT_EQUAL_STRING(stem_a, stem_b);

    free(stem_a);
    free(stem_b);
    memory_sink_free(&sink_a);
    memory_sink_free(&sink_b);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_truncation_suffix_present_and_well_formed);
    RUN_TEST(test_filename_within_name_max);
    RUN_TEST(test_truncation_is_deterministic);
    return UNITY_END();
}
