/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * End-to-end emit + compile test. Parses a small schema, runs
 * the analyser, emits the generated C, writes the output to a
 * temporary directory, then invokes the C compiler to confirm
 * the emitted code is syntactically valid against the runtime
 * headers.
 *
 * This is the milestone "C parser successfully generates C code"
 * gate.
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

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

#ifndef ZSP_REPO_ROOT
#define ZSP_REPO_ROOT ""
#endif

static zserio_allocator_t libc_alloc;

void setUp(void)  { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* Recursively create directory components in `path` (everything
 * up to the last `/` is treated as a directory). */
static void mkdirs_for(const char *path)
{
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    while ((len > 0U) && (buf[len - 1U] != '/')) {
        len -= 1U;
    }
    if (len == 0U) return;
    buf[len - 1U] = '\0';
    /* Walk forward creating each component. */
    for (size_t i = 1U; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            (void)mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    (void)mkdir(buf, 0755);
}

/* Disk-write sink: writes each emitted file to ${out_dir}/${path}. */
typedef struct disk_sink {
    const char *out_dir;
    bool        ok;
} disk_sink_t;

static bool disk_write(void *ud, const char *path,
                       const char *bytes, size_t len)
{
    disk_sink_t *const s = (disk_sink_t *)ud;
    char full[1200];
    (void)snprintf(full, sizeof(full), "%s/%s", s->out_dir, path);
    mkdirs_for(full);
    FILE *const f = fopen(full, "wb");
    if (f == NULL) { s->ok = false; return false; }
    const size_t wrote = fwrite(bytes, 1U, len, f);
    (void)fclose(f);
    if (wrote != len) { s->ok = false; return false; }
    return true;
}

static void test_emit_and_compile_simple_struct(void)
{
    /* Set up a temp dir under /tmp. */
    char out_dir[256];
    (void)snprintf(out_dir, sizeof(out_dir), "/tmp/zsp_emit_test_%u",
                   (unsigned)getpid());
    /* Best-effort wipe: clean then recreate. */
    char rm_cmd[300];
    (void)snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", out_dir);
    (void)system(rm_cmd);
    (void)mkdir(out_dir, 0755);

    /* Parse a tiny schema, analyse, emit. */
    static const uint8_t INPUT[] =
            "package telemetry;\n"
            "struct Frame {\n"
            "    uint32 sensor_id;\n"
            "    string device_label;\n"
            "};\n";
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
            &src, "test.zs", 7U, INPUT, sizeof(INPUT) - 1U, &fid));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_parse_unit(&src, fid, &arena, &diag, &unit));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyse_unit(&sema, unit));

    disk_sink_t sink = { out_dir, true };
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_emit_unit(unit, &arena, &diag, &src,
                                              disk_write, &sink));
    TEST_ASSERT_TRUE(sink.ok);

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);

    /* Smoke compile: invoke cc against the runtime headers +
     * generated sources. */
    char cc_cmd[2048];
    (void)snprintf(cc_cmd, sizeof(cc_cmd),
                   "cc -std=c99 -Wall -Wextra -Werror "
                   "-I'%s/runtime/include' "
                   "-c '%s/telemetry/frame.c' "
                   "-o '%s/frame.o' 2>'%s/cc.err'",
                   ZSP_REPO_ROOT, out_dir, out_dir, out_dir);
    const int rc = system(cc_cmd);
    if (rc != 0) {
        /* Print compiler diagnostics if compilation failed. */
        char err_path[512];
        (void)snprintf(err_path, sizeof(err_path), "%s/cc.err", out_dir);
        FILE *const f = fopen(err_path, "rb");
        if (f != NULL) {
            char buf[4096];
            const size_t n = fread(buf, 1U, sizeof(buf) - 1U, f);
            buf[n] = '\0';
            (void)fclose(f);
            (void)fprintf(stderr, "\ngenerated code failed to compile:\n%s\n", buf);
        }
        TEST_FAIL_MESSAGE("emitted C did not compile against runtime headers");
    }
    /* Cleanup */
    (void)system(rm_cmd);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_emit_and_compile_simple_struct);
    return UNITY_END();
}
