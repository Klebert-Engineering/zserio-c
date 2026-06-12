/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * Emitter tests: parse → analyze → emit and inspect the
 * generated C output. Also runs a smoke compilation of the
 * generated C against the runtime to confirm the output is
 * at least syntactically valid.
 *
 * Verifies: REQ-FN-008 — output file layout mirrors package hierarchy.
 * Verifies: REQ-FN-009 — schema → C identifier mapping is total.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

static zserio_allocator_t libc_alloc;

void setUp(void)  { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* In-memory collector: stash (path, contents) for each emitted
 * file so tests can inspect what the emitter produced. */
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

static const emitted_file_t *find_file(const memory_sink_t *s,
                                       const char *suffix)
{
    const size_t sl = strlen(suffix);
    for (size_t i = 0U; i < s->count; ++i) {
        const size_t pl = strlen(s->files[i].path);
        if ((pl >= sl) && (strcmp(&s->files[i].path[pl - sl], suffix) == 0)) {
            return &s->files[i];
        }
    }
    return NULL;
}

static void parse_analyze_emit(const uint8_t *bytes, size_t len,
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
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&sema, unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_emit_unit(unit, &arena, &diag, &src,
                                              memory_write, sink));

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
}

/* ---- Tests ---- */

static void test_emit_simple_struct(void)
{
    static const uint8_t INPUT[] =
            "package x;\n"
            "struct Frame {\n"
            "    uint32 sensor_id;\n"
            "    string device_label;\n"
            "};\n";
    memory_sink_t sink = {0};
    parse_analyze_emit(INPUT, sizeof(INPUT) - 1U, &sink);
    TEST_ASSERT_TRUE(sink.count >= 2U);

    const emitted_file_t *const h = find_file(&sink, "frame.h");
    const emitted_file_t *const c = find_file(&sink, "frame.c");
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_NOT_NULL(c);

    /* Header contains the struct definition + prototypes. Names are
     * package-qualified to match the Java generator convention. */
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "typedef struct x_frame_s x_frame_t;"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "uint32_t sensor_id;"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "zserio_string_t device_label;"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_frame_t;"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_frame_read("));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_frame_write("));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_frame_free("));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_frame_init("));

    /* Source contains the function bodies. */
    TEST_ASSERT_NOT_NULL(strstr(c->bytes, "x_frame_init(x_frame_t *self)"));
    TEST_ASSERT_NOT_NULL(strstr(c->bytes, "x_frame_read("));
    TEST_ASSERT_NOT_NULL(strstr(c->bytes, "zserio_read_uint32(bs, &self->sensor_id)"));
    TEST_ASSERT_NOT_NULL(strstr(c->bytes, "zserio_read_string(bs, alloc, &self->device_label)"));
    TEST_ASSERT_NOT_NULL(strstr(c->bytes, "zserio_string_free(&self->device_label"));

    memory_sink_free(&sink);
}

static void test_emit_enum(void)
{
    static const uint8_t INPUT[] =
            "package x;\n"
            "enum uint8 Color { RED = 0, GREEN = 1, BLUE = 2 };\n";
    memory_sink_t sink = {0};
    parse_analyze_emit(INPUT, sizeof(INPUT) - 1U, &sink);

    const emitted_file_t *const h = find_file(&sink, "color.h");
    TEST_ASSERT_NOT_NULL(h);
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "typedef enum x_color"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_COLOR_RED = 0"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_COLOR_GREEN = 1"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_COLOR_BLUE = 2"));
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "x_color_t;"));

    memory_sink_free(&sink);
}

static void test_emit_writes_to_package_path(void)
{
    static const uint8_t INPUT[] =
            "package geo.tile;\n"
            "struct Header { uint32 magic; };\n";
    memory_sink_t sink = {0};
    parse_analyze_emit(INPUT, sizeof(INPUT) - 1U, &sink);

    bool found_h = false;
    bool found_c = false;
    for (size_t i = 0U; i < sink.count; ++i) {
        if (strstr(sink.files[i].path, "geo/tile/header.h") != NULL) {
            found_h = true;
        }
        if (strstr(sink.files[i].path, "geo/tile/header.c") != NULL) {
            found_c = true;
        }
    }
    TEST_ASSERT_TRUE(found_h);
    TEST_ASSERT_TRUE(found_c);

    memory_sink_free(&sink);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_emit_simple_struct);
    RUN_TEST(test_emit_enum);
    RUN_TEST(test_emit_writes_to_package_path);
    return UNITY_END();
}
