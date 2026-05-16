/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * zserio-c-gen: command-line driver for the C-native parser
 * pipeline. Reads a `.zs` schema, parses + analyses + emits
 * the generated C code to the specified output directory.
 *
 * Usage:
 *   zserio-c-gen <input.zs> -c <output-dir>
 *
 * This is the entry point integrators use in place of the
 * Java-based ZserioTool for the C target.
 */

#define _POSIX_C_SOURCE 200809L

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "zsparser/arena.h"
#include "zsparser/ast.h"
#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/parser.h"
#include "zsparser/sema.h"
#include "zsparser/source.h"

static void usage(const char *argv0)
{
    (void)fprintf(stderr,
                  "usage: %s <input.zs> -c <output-dir>\n"
                  "\n"
                  "  <input.zs>     schema file to compile\n"
                  "  -c <output>    directory for emitted .c / .h files\n",
                  argv0);
}

static uint8_t *read_file_(const char *path, size_t *out_len)
{
    FILE *const f = fopen(path, "rb");
    if (f == NULL) {
        (void)fprintf(stderr, "error: cannot open '%s'\n", path);
        return NULL;
    }
    (void)fseek(f, 0L, SEEK_END);
    const long sz = ftell(f);
    if ((sz < 0L) || (fseek(f, 0L, SEEK_SET) != 0)) {
        (void)fclose(f);
        return NULL;
    }
    uint8_t *const buf = (uint8_t *)malloc((size_t)sz + 1U);
    if (buf == NULL) {
        (void)fclose(f);
        return NULL;
    }
    const size_t got = fread(buf, 1U, (size_t)sz, f);
    (void)fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        return NULL;
    }
    buf[sz] = (uint8_t)'\0';
    *out_len = (size_t)sz;
    return buf;
}

static void mkdirs_for(const char *path)
{
    char buf[2048];
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

typedef struct out_ctx {
    const char *base_dir;
    bool        ok;
    size_t      files;
} out_ctx_t;

static bool out_write(void *ud, const char *rel,
                      const char *bytes, size_t len)
{
    out_ctx_t *const c = (out_ctx_t *)ud;
    char full[4096];
    (void)snprintf(full, sizeof(full), "%s/%s", c->base_dir, rel);
    mkdirs_for(full);
    FILE *const f = fopen(full, "wb");
    if (f == NULL) { c->ok = false; return false; }
    const size_t wrote = fwrite(bytes, 1U, len, f);
    (void)fclose(f);
    if (wrote != len) { c->ok = false; return false; }
    c->files += 1U;
    return true;
}

/* Stdout sink for diagnostics. */
static void diag_to_stderr(void *ud, const char *bytes, size_t len)
{
    (void)ud;
    (void)fwrite(bytes, 1U, len, stderr);
}

int main(int argc, char **argv)
{
    if (argc < 4) { usage(argv[0]); return 2; }

    const char *input  = NULL;
    const char *outdir = NULL;
    for (int i = 1; i < argc; ++i) {
        if ((strcmp(argv[i], "-c") == 0) && ((i + 1) < argc)) {
            outdir = argv[i + 1];
            i += 1;
        }
        else {
            input = argv[i];
        }
    }
    if ((input == NULL) || (outdir == NULL)) {
        usage(argv[0]);
        return 2;
    }

    size_t len = 0U;
    uint8_t *const bytes = read_file_(input, &len);
    if (bytes == NULL) return 1;

    zserio_allocator_t libc = zserio_allocator_libc();

    zsp_source_t         src;
    zsp_arena_t          arena;
    zsp_diag_collector_t diag;
    zsp_file_id_t        fid;
    zsp_ast_node_t      *unit = NULL;
    zsp_sema_t           sema;

    if ((zsp_source_init(&src, &libc) != ZSERIO_OK)
            || (zsp_arena_init(&arena, &libc) != ZSERIO_OK)
            || (zsp_diag_init(&diag, &libc, 64U) != ZSERIO_OK)) {
        (void)fprintf(stderr, "error: session init failed\n");
        free(bytes);
        return 1;
    }
    if (zsp_source_add_borrowed(&src, input, strlen(input),
                                bytes, len, &fid) != ZSERIO_OK) {
        (void)fprintf(stderr, "error: source register failed\n");
        free(bytes);
        return 1;
    }

    int rc = 0;
    if (zsp_parse_unit(&src, fid, &arena, &diag, &unit) != ZSERIO_OK) {
        rc = 1;
    }
    if ((rc == 0)
            && (zsp_sema_init(&sema, &src, &arena, &diag) != ZSERIO_OK)) {
        rc = 1;
    }
    if (rc == 0) {
        (void)zsp_sema_analyse_unit(&sema, unit);
    }
    if ((rc == 0) && (zsp_diag_error_count(&diag) == 0U)) {
        out_ctx_t out = { outdir, true, 0U };
        if (zsp_emit_unit(unit, &arena, &diag, &src, out_write, &out) != ZSERIO_OK) {
            (void)fprintf(stderr, "error: emit failed\n");
            rc = 1;
        }
        else if (!out.ok) {
            (void)fprintf(stderr, "error: file write failed\n");
            rc = 1;
        }
        else {
            (void)fprintf(stderr, "wrote %zu files under %s\n",
                          out.files, outdir);
        }
    }

    /* Format diagnostics to stderr. */
    (void)zsp_diag_format_gcc(&diag, &src, diag_to_stderr, NULL);

    if (zsp_diag_error_count(&diag) > 0U) {
        rc = 1;
    }

    zsp_diag_dispose(&diag);
    zsp_arena_dispose(&arena);
    zsp_source_dispose(&src);
    free(bytes);
    return rc;
}
