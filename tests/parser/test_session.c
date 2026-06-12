/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * Multi-file compilation-session tests: load a top schema +
 * its imported dependencies, analyze across the combined
 * symbol table, assert cross-file references resolve.
 *
 * Verifies: REQ-FN-005 — import resolution rules (search-order
 *                        and parse-once-by-canonical-path).
 * Verifies: REQ-FN-006 — import-graph cycle handling
 *                        (cycle dedup test ensures bounded work).
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "unity.h"

#include "zsparser/diag.h"
#include "zsparser/session.h"
#include "zsparser/source.h"

#ifndef ZSP_CORPUS_ROOT
#define ZSP_CORPUS_ROOT ""
#endif

void setUp(void) {}
void tearDown(void) {}

static void write_file(const char *path, const char *content)
{
    /* mkdir for parent dir. */
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    while ((len > 0U) && (buf[len - 1U] != '/')) len -= 1U;
    if (len > 1U) {
        buf[len - 1U] = '\0';
        for (size_t i = 1U; i < (len - 1U); ++i) {
            if (buf[i] == '/') { buf[i] = '\0'; (void)mkdir(buf, 0755); buf[i] = '/'; }
        }
        (void)mkdir(buf, 0755);
    }
    FILE *const f = fopen(path, "wb");
    TEST_ASSERT_NOT_NULL(f);
    (void)fwrite(content, 1U, strlen(content), f);
    (void)fclose(f);
}

static void test_session_loads_dependencies_transitively(void)
{
    char root[256];
    (void)snprintf(root, sizeof(root), "/tmp/zsp_session_test_%u",
                   (unsigned)getpid());
    char rm_cmd[300];
    (void)snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", root);
    (void)system(rm_cmd);
    (void)mkdir(root, 0755);

    char path[512];
    (void)snprintf(path, sizeof(path), "%s/geometry.zs", root);
    write_file(path,
               "package geometry;\n"
               "struct Point { int32 x; int32 y; };\n");

    (void)snprintf(path, sizeof(path), "%s/roads.zs", root);
    write_file(path,
               "package roads;\n"
               "import geometry.*;\n"
               "struct Stop { uint32 id; Point location; };\n");

    const char *roots[] = { root };
    zsp_session_t s;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_init(&s, roots, 1U));

    /* Load the top file. Dependencies (`geometry.zs`) load
     * transitively via the import directive. */
    char topfile[512];
    (void)snprintf(topfile, sizeof(topfile), "%s/roads.zs", root);
    zsp_ast_node_t *top_unit = NULL;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_load(&s, topfile, &top_unit));
    TEST_ASSERT_NOT_NULL(top_unit);
    /* Two files should be loaded. */
    TEST_ASSERT_EQUAL_size_t(2U, s.n_files);

    /* Analyze all units. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_analyze(&s));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&s.diag));

    /* Verify the Point reference in Stop resolved. */
    zsp_ast_node_t *const stop = top_unit->u.unit.decls[0];
    zsp_ast_node_t *const loc  = stop->u.compound.members[1];
    TEST_ASSERT_NOT_NULL(loc->u.field.type->resolved);

    zsp_session_dispose(&s);
    (void)system(rm_cmd);
}

static void test_session_handles_missing_dependency_gracefully(void)
{
    char root[256];
    (void)snprintf(root, sizeof(root), "/tmp/zsp_session_test2_%u",
                   (unsigned)getpid());
    char rm_cmd[300];
    (void)snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", root);
    (void)system(rm_cmd);
    (void)mkdir(root, 0755);

    char path[512];
    (void)snprintf(path, sizeof(path), "%s/only.zs", root);
    write_file(path,
               "package only;\n"
               "import nonexistent.*;\n"
               "struct One { uint32 v; };\n");

    const char *roots[] = { root };
    zsp_session_t s;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_init(&s, roots, 1U));
    zsp_ast_node_t *unit = NULL;
    /* Load should succeed even though the import target is
     * missing — missing dependencies surface as unresolved
     * type-refs during analysis (best-effort recovery). */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_load(&s, path, &unit));
    TEST_ASSERT_NOT_NULL(unit);
    TEST_ASSERT_EQUAL_size_t(1U, s.n_files);

    /* Analysis runs without aborting; the struct definition is
     * still picked up. */
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_analyze(&s));

    zsp_session_dispose(&s);
    (void)system(rm_cmd);
}

static void test_session_dedups_already_loaded_files(void)
{
    char root[256];
    (void)snprintf(root, sizeof(root), "/tmp/zsp_session_test3_%u",
                   (unsigned)getpid());
    char rm_cmd[300];
    (void)snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", root);
    (void)system(rm_cmd);
    (void)mkdir(root, 0755);

    /* A→B, B→A: cycle in the import graph. Session must
     * deduplicate to avoid infinite recursion. */
    char path[512];
    (void)snprintf(path, sizeof(path), "%s/a.zs", root);
    write_file(path,
               "package a;\n"
               "import b.*;\n"
               "struct A { uint32 v; };\n");
    (void)snprintf(path, sizeof(path), "%s/b.zs", root);
    write_file(path,
               "package b;\n"
               "import a.*;\n"
               "struct B { uint32 w; };\n");

    const char *roots[] = { root };
    zsp_session_t s;
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_init(&s, roots, 1U));
    char top[512];
    (void)snprintf(top, sizeof(top), "%s/a.zs", root);
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_session_load(&s, top, NULL));
    /* Two files total — the cycle didn't load A twice. */
    TEST_ASSERT_EQUAL_size_t(2U, s.n_files);

    zsp_session_dispose(&s);
    (void)system(rm_cmd);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_session_loads_dependencies_transitively);
    RUN_TEST(test_session_handles_missing_dependency_gracefully);
    RUN_TEST(test_session_dedups_already_loaded_files);
    return UNITY_END();
}
