/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 *
 * End-to-end service + pub/sub generation test. Parses a schema with a
 * `service` (two methods) and a `pubsub` (a published and a subscribed
 * message), emits the generated C to a temp dir, then compiles and
 * links the generated code together with the runtime and the
 * round-trip driver fixture (service_pubsub_roundtrip.c) and runs it.
 * The driver exercises the generated client/server dispatch and the
 * publish/subscribe glue through mock transports.
 *
 * Verifies: REQ-FN-027 — service and pub/sub interface generation.
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

void setUp(void) { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

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
    for (size_t i = 1U; i < len; ++i) {
        if (buf[i] == '/') {
            buf[i] = '\0';
            (void)mkdir(buf, 0755);
            buf[i] = '/';
        }
    }
    (void)mkdir(buf, 0755);
}

typedef struct disk_sink {
    const char *out_dir;
    bool        ok;
} disk_sink_t;

static bool disk_write(void *ud, const char *path, const char *bytes, size_t len)
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

/* Copy the round-trip driver fixture from the source tree into the temp
 * dir so it can be compiled alongside the generated code. */
static bool copy_driver(const char *out_dir)
{
    char src_path[1024];
    (void)snprintf(src_path, sizeof(src_path),
                   "%s/tests/parser/service_pubsub_roundtrip.c", ZSP_REPO_ROOT);
    FILE *const in = fopen(src_path, "rb");
    if (in == NULL) return false;
    char dst_path[1200];
    (void)snprintf(dst_path, sizeof(dst_path), "%s/driver.c", out_dir);
    FILE *const out = fopen(dst_path, "wb");
    if (out == NULL) { (void)fclose(in); return false; }
    char   buf[4096];
    size_t n;
    bool   ok = true;
    while ((n = fread(buf, 1U, sizeof(buf), in)) > 0U) {
        if (fwrite(buf, 1U, n, out) != n) { ok = false; break; }
    }
    (void)fclose(in);
    (void)fclose(out);
    return ok;
}

static void test_service_pubsub_roundtrip(void)
{
    char out_dir[256];
    (void)snprintf(out_dir, sizeof(out_dir), "/tmp/zsp_svc_test_%u", (unsigned)getpid());
    char rm_cmd[300];
    (void)snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", out_dir);
    (void)system(rm_cmd);
    (void)mkdir(out_dir, 0755);

    /* Published and subscribed messages share a topic so the mock
     * broker connects them. */
    static const uint8_t INPUT[] =
            "package calc;\n"
            "struct Request { uint32 a; uint32 b; };\n"
            "struct Response { uint32 result; };\n"
            "struct Sample { uint32 sensor_id; uint32 value; };\n"
            "service Calculator {\n"
            "    Response add(Request);\n"
            "    Response multiply(Request);\n"
            "};\n"
            "pubsub SensorBus {\n"
            "    publish topic(\"sensors/sample\") Sample published;\n"
            "    subscribe topic(\"sensors/sample\") Sample commanded;\n"
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
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&sema, unit));

    disk_sink_t sink = {out_dir, true};
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_emit_unit(unit, &arena, &diag, &src, disk_write, &sink));
    TEST_ASSERT_TRUE(sink.ok);

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);

    TEST_ASSERT_TRUE_MESSAGE(copy_driver(out_dir), "could not stage round-trip driver");

    /* Compile + link the generated code, the runtime, and the driver,
     * then run it. The generated package headers include their siblings
     * by bare name, resolved relative to each file's own directory; the
     * driver uses `calc/...` and `zserio/...` paths, covered by the two
     * -I flags. */
    char cc_cmd[2048];
    (void)snprintf(cc_cmd, sizeof(cc_cmd),
                   "cc -std=c99 -Wall -Wextra -Werror "
                   "-I'%s/runtime/include' -I'%s' "
                   "'%s/driver.c' '%s'/calc/*.c '%s/runtime/src'/*.c "
                   "-o '%s/driver' 2>'%s/cc.err'",
                   ZSP_REPO_ROOT, out_dir, out_dir, out_dir, ZSP_REPO_ROOT,
                   out_dir, out_dir);
    const int cc_rc = system(cc_cmd);
    if (cc_rc != 0) {
        char err_path[512];
        (void)snprintf(err_path, sizeof(err_path), "%s/cc.err", out_dir);
        FILE *const f = fopen(err_path, "rb");
        if (f != NULL) {
            char         buf[4096];
            const size_t n = fread(buf, 1U, sizeof(buf) - 1U, f);
            buf[n] = '\0';
            (void)fclose(f);
            (void)fprintf(stderr, "\ngenerated service/pubsub code failed to build:\n%s\n", buf);
        }
        TEST_FAIL_MESSAGE("generated service/pubsub code did not compile/link");
    }

    char run_cmd[512];
    (void)snprintf(run_cmd, sizeof(run_cmd), "'%s/driver'", out_dir);
    const int run_rc = system(run_cmd);
    TEST_ASSERT_EQUAL_MESSAGE(0, run_rc, "service/pubsub round-trip driver failed at runtime");

    (void)system(rm_cmd);
}

/* Discard sink: accepts every emitted file without storing it. */
static bool null_write(void *ud, const char *path, const char *bytes, size_t len)
{
    (void)ud;
    (void)path;
    (void)bytes;
    (void)len;
    return true;
}

/* A pubsub topic that is not a plain string literal must be rejected
 * with an error diagnostic instead of baking a wrong topic into the
 * generated glue. */
static void test_non_literal_topic_rejected(void)
{
    static const uint8_t INPUT[] =
            "package t;\n"
            "const string PREFIX = \"sensors/x\";\n"
            "struct M { uint32 v; };\n"
            "pubsub P {\n"
            "    publish topic(PREFIX) M m;\n"
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
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_init(&sema, &src, &arena, &diag));
    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_sema_analyze_unit(&sema, unit));
    TEST_ASSERT_EQUAL_size_t(0U, zsp_diag_error_count(&diag));

    TEST_ASSERT_EQUAL(ZSERIO_OK, zsp_emit_unit(unit, &arena, &diag, &src,
                                              null_write, NULL));
    TEST_ASSERT_TRUE_MESSAGE(zsp_diag_error_count(&diag) >= 1U,
                             "non-literal pubsub topic must produce an error diagnostic");

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_service_pubsub_roundtrip);
    RUN_TEST(test_non_literal_topic_rejected);
    return UNITY_END();
}
