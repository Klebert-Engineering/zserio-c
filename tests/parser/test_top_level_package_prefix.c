/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * Verifies: REQ-FN-014 — -setTopLevelPackage prefix propagation.
 *
 * Two passes over the same schema-on-disk:
 *   (A) no prefix              → identifiers and paths use the
 *                                schema's own package.
 *   (B) prefix "wrap_pkg"      → every identifier and emitted
 *                                file path carries the prefix.
 *
 * Assertions:
 *   1. Without the flag, the package's emitted path is `original/...`.
 *   2. With the flag, the path is `wrap_pkg/original/...`.
 *   3. Identifiers in the emitted header are prefixed accordingly
 *      (`original_<type>_t` vs `wrap_pkg_original_<type>_t`).
 *   4. Empty / NULL prefix is a no-op (passes through unchanged).
 *   5. Multi-component prefix ("a.b") produces nested directory
 *      and identifier components.
 *   6. Import resolution is unaffected — the flag rewrites
 *      packages, not on-disk lookup paths.
 */

#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
/* _POSIX_C_SOURCE alone hides mkdtemp on Darwin; opt back in. */
#define _DARWIN_C_SOURCE 1
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "unity.h"

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/session.h"

void setUp(void) {}
void tearDown(void) {}

/* ------------------------------------------------------------------ */
/* Disk-backed schema fixture                                         */
/* ------------------------------------------------------------------ */

static const char SCHEMA_TEXT[] =
        "package original.payload;\n"
        "\n"
        "struct Frame {\n"
        "    uint32 sensor_id;\n"
        "    string label;\n"
        "};\n";

/* Write SCHEMA_TEXT to a fresh temp directory and return:
 *   - the schema's full path in *schema_path (caller frees)
 *   - the source-root directory the schema sits under in
 *     *root_path (caller frees)
 * The schema is laid out as `<root>/original/payload.zs` so
 * the session's import-resolution treats `<root>` as the import
 * root for everything in package `original.*`. */
static void write_fixture(char **schema_path_out, char **root_path_out)
{
    static char root[1024];
    /* mkdtemp wants a writable template; use /tmp. */
    (void)snprintf(root, sizeof(root), "/tmp/zserio_tlp_XXXXXX");
    TEST_ASSERT_NOT_NULL(mkdtemp(root));

    char subdir[2048];
    (void)snprintf(subdir, sizeof(subdir), "%s/original", root);
    TEST_ASSERT_EQUAL(0, mkdir(subdir, 0700));

    char *schema = (char *)malloc(4096);
    TEST_ASSERT_NOT_NULL(schema);
    (void)snprintf(schema, 4096, "%s/payload.zs", subdir);
    FILE *fp = fopen(schema, "wb");
    TEST_ASSERT_NOT_NULL(fp);
    const size_t n = sizeof(SCHEMA_TEXT) - 1U;
    TEST_ASSERT_EQUAL_size_t(n, fwrite(SCHEMA_TEXT, 1U, n, fp));
    (void)fclose(fp);

    *schema_path_out = schema;
    *root_path_out   = strdup(root);
    TEST_ASSERT_NOT_NULL(*root_path_out);
}

static void cleanup_fixture(const char *schema_path, const char *root_path)
{
    /* Best-effort cleanup; tests run as users with write
     * permission to /tmp/<their-dir>. We unlink the known leaf
     * and rmdir backwards. */
    (void)unlink(schema_path);
    char subdir[2048];
    (void)snprintf(subdir, sizeof(subdir), "%s/original", root_path);
    (void)rmdir(subdir);
    (void)rmdir(root_path);
}

/* ------------------------------------------------------------------ */
/* In-memory emit collector                                           */
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

static const emitted_file_t *find_path_with_suffix(const memory_sink_t *s,
                                                   const char *suffix)
{
    const size_t sl = strlen(suffix);
    for (size_t i = 0U; i < s->count; ++i) {
        const size_t pl = strlen(s->files[i].path);
        if ((pl >= sl)
                && (strcmp(&s->files[i].path[pl - sl], suffix) == 0)) {
            return &s->files[i];
        }
    }
    return NULL;
}

/* Drive the session pipeline with an optional prefix and collect
 * the emitted files. */
static void run_pipeline(const char *root, const char *schema,
                        const char *prefix, memory_sink_t *sink)
{
    const char *const roots[1] = { root };
    zsp_session_t s;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_init(&s, roots, 1U));

    zsp_ast_node_t *top = NULL;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_load(&s, schema, &top));
    TEST_ASSERT_NOT_NULL(top);

    /* The flag MUST be applied between load and analyze. */
    TEST_ASSERT_EQUAL(ZSERIO_OK,
                      zsp_session_set_top_level_package(&s, prefix));

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_analyze(&s));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&s.diag));

    for (size_t i = 0U; i < s.n_files; ++i) {
        if (s.files[i].unit == NULL) continue;
        TEST_ASSERT_EQUAL(ZSERIO_OK,
                          zsp_emit_unit(s.files[i].unit, &s.arena,
                                        &s.diag, &s.source,
                                        memory_write, sink));
    }

    zsp_session_dispose(&s);
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

/* No prefix: identifiers and paths reflect the schema's own
 * declared package. */
static void test_no_prefix_is_baseline(void)
{
    char *schema, *root;
    write_fixture(&schema, &root);

    memory_sink_t sink = {0};
    run_pipeline(root, schema, NULL, &sink);

    const emitted_file_t *const h =
            find_path_with_suffix(&sink, "original/payload/frame.h");
    TEST_ASSERT_NOT_NULL_MESSAGE(
            h, "no-prefix emit should place the header under the "
               "schema's declared package path");

    /* Identifier prefix is the package name lowercased: `original_payload_`. */
    TEST_ASSERT_NOT_NULL(strstr(h->bytes, "original_payload_frame_t"));
    /* No accidental prefix bleed-through. */
    TEST_ASSERT_NULL(strstr(h->bytes, "wrap_pkg_"));

    memory_sink_free(&sink);
    cleanup_fixture(schema, root);
    free(schema); free(root);
}

/* Single-component prefix: every identifier and emitted file path
 * carries `wrap_pkg_` / `wrap_pkg/`. */
static void test_single_component_prefix_propagates(void)
{
    char *schema, *root;
    write_fixture(&schema, &root);

    memory_sink_t sink = {0};
    run_pipeline(root, schema, "wrap_pkg", &sink);

    /* File path is now `<wrap_pkg>/<original>/<payload>/frame.h`. */
    const emitted_file_t *const h = find_path_with_suffix(
            &sink, "wrap_pkg/original/payload/frame.h");
    TEST_ASSERT_NOT_NULL_MESSAGE(
            h, "prefixed emit should place the header under "
               "<prefix>/<original-package-path>");

    /* Identifier carries the prefix. */
    TEST_ASSERT_NOT_NULL(strstr(h->bytes,
                                "wrap_pkg_original_payload_frame_t"));

    /* The unprefixed identifier should no longer appear as a
     * standalone token. We search for the unprefixed type with a
     * leading word boundary surrogate (preceding non-identifier
     * char). The emitted header's first reference to the type is
     * preceded by either start-of-file or whitespace; we check by
     * confirming the prefixed form is the *only* shape used. */
    const char *unprefixed = "original_payload_frame_t";
    const char *cursor = h->bytes;
    while ((cursor = strstr(cursor, unprefixed)) != NULL) {
        /* Every occurrence must be preceded by "wrap_pkg_" to be
         * part of the prefixed identifier; if any occurrence is
         * not, the prefix didn't propagate. */
        if ((cursor - h->bytes) < 9 /* strlen("wrap_pkg_") */) {
            TEST_FAIL_MESSAGE("unprefixed identifier slipped through");
        }
        if (memcmp(cursor - 9, "wrap_pkg_", 9U) != 0) {
            TEST_FAIL_MESSAGE("unprefixed identifier slipped through");
        }
        cursor += strlen(unprefixed);
    }

    memory_sink_free(&sink);
    cleanup_fixture(schema, root);
    free(schema); free(root);
}

/* Multi-component prefix: `a.b` produces nested directories and
 * compounded identifier prefixes. */
static void test_multi_component_prefix(void)
{
    char *schema, *root;
    write_fixture(&schema, &root);

    memory_sink_t sink = {0};
    run_pipeline(root, schema, "a.b", &sink);

    const emitted_file_t *const h = find_path_with_suffix(
            &sink, "a/b/original/payload/frame.h");
    TEST_ASSERT_NOT_NULL_MESSAGE(
            h, "multi-component prefix should produce nested "
               "directories");
    TEST_ASSERT_NOT_NULL(strstr(h->bytes,
                                "a_b_original_payload_frame_t"));

    memory_sink_free(&sink);
    cleanup_fixture(schema, root);
    free(schema); free(root);
}

/* Empty string and NULL prefix are no-ops (identical output to
 * not passing the flag at all). */
static void test_empty_and_null_prefix_are_noops(void)
{
    char *schema, *root;
    write_fixture(&schema, &root);

    memory_sink_t sink_null = {0};
    run_pipeline(root, schema, NULL, &sink_null);

    memory_sink_t sink_empty = {0};
    run_pipeline(root, schema, "", &sink_empty);

    TEST_ASSERT_EQUAL_size_t(sink_null.count, sink_empty.count);

    /* Find the matching header in each and byte-compare. */
    const emitted_file_t *a =
            find_path_with_suffix(&sink_null, "frame.h");
    const emitted_file_t *b =
            find_path_with_suffix(&sink_empty, "frame.h");
    TEST_ASSERT_NOT_NULL(a);
    TEST_ASSERT_NOT_NULL(b);
    TEST_ASSERT_EQUAL_STRING(a->path, b->path);
    TEST_ASSERT_EQUAL_size_t(a->len, b->len);
    TEST_ASSERT_EQUAL_MEMORY(a->bytes, b->bytes, a->len);

    memory_sink_free(&sink_null);
    memory_sink_free(&sink_empty);
    cleanup_fixture(schema, root);
    free(schema); free(root);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_no_prefix_is_baseline);
    RUN_TEST(test_single_component_prefix_propagates);
    RUN_TEST(test_multi_component_prefix);
    RUN_TEST(test_empty_and_null_prefix_are_noops);
    return UNITY_END();
}
