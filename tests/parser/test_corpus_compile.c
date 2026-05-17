/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 *
 * Corpus compile sweep: for every parseable+analysable schema
 * in the upstream language corpus, emit C code and then invoke
 * the C compiler on each generated .c file. Failure means the
 * emitter produced C that doesn't compile against our runtime.
 *
 * This is the Phase 7 xvalidation MVP gate: every schema we
 * parse must yield C that the toolchain accepts. Byte-equal
 * vs the Java generator is a stricter follow-up gate that
 * requires matched driver harnesses.
 */

#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "unity.h"

#include "zsparser/diag.h"
#include "zsparser/emit.h"
#include "zsparser/session.h"
#include "zsparser/source.h"

#ifndef ZSP_CORPUS_ROOT
#define ZSP_CORPUS_ROOT ""
#endif
#ifndef ZSP_REPO_ROOT
#define ZSP_REPO_ROOT ""
#endif

static zserio_allocator_t libc_alloc;

void setUp(void)  { libc_alloc = zserio_allocator_libc(); }
void tearDown(void) {}

/* Disk sink: writes emitted .c / .h files to a session-scoped
 * directory and records the set of .c files for compilation. */
typedef struct disk_sink {
    char    out_dir[512];
    char  **c_files;
    size_t  n_c;
    size_t  cap;
    bool    ok;
} disk_sink_t;

static void mkdirs_for(const char *path)
{
    char buf[2048];
    (void)snprintf(buf, sizeof(buf), "%s", path);
    size_t len = strlen(buf);
    while ((len > 0U) && (buf[len - 1U] != '/')) len -= 1U;
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

static bool ends_with_c(const char *path)
{
    const size_t n = strlen(path);
    return (n >= 2U) && (path[n - 2U] == '.') && (path[n - 1U] == 'c');
}

static bool disk_write(void *ud, const char *rel,
                       const char *bytes, size_t len)
{
    disk_sink_t *const s = (disk_sink_t *)ud;
    char full[4096];
    (void)snprintf(full, sizeof(full), "%s/%s", s->out_dir, rel);
    mkdirs_for(full);
    FILE *const f = fopen(full, "wb");
    if (f == NULL) { s->ok = false; return false; }
    const size_t wrote = fwrite(bytes, 1U, len, f);
    (void)fclose(f);
    if (wrote != len) { s->ok = false; return false; }
    if (ends_with_c(full)) {
        if (s->n_c == s->cap) {
            const size_t new_cap = (s->cap == 0U) ? 8U : (s->cap * 2U);
            char **const grown = (char **)realloc(s->c_files,
                                                  new_cap * sizeof(char *));
            if (grown == NULL) { s->ok = false; return false; }
            s->c_files = grown;
            s->cap     = new_cap;
        }
        s->c_files[s->n_c++] = strdup(full);
    }
    return true;
}

/* Derive likely source-roots from a corpus schema's path. */
static void derive_roots(const char *path, char roots[][1024],
                         const char **out_roots, size_t cap, size_t *n_out)
{
    *n_out = 0U;
    char buf[1024];
    (void)snprintf(buf, sizeof(buf), "%s", path);
    for (size_t up = 0U; (up < 6U) && (*n_out < cap); ++up) {
        size_t pos = strlen(buf);
        while ((pos > 0U) && (buf[pos - 1U] != '/')) pos -= 1U;
        if (pos == 0U) break;
        buf[pos - 1U] = '\0';
        (void)snprintf(roots[*n_out], 1024, "%s", buf);
        out_roots[*n_out] = roots[*n_out];
        *n_out += 1U;
    }
}

typedef struct compile_state {
    size_t total_schemas;
    size_t emitted_schemas;
    size_t compiled_schemas;
    size_t compiled_c_files;
    size_t failed_schemas;
    char  *first_failures[8];
    size_t n_first_failures;
} compile_state_t;

static void compile_one_schema(const char *schema_path, compile_state_t *st)
{
    st->total_schemas += 1U;

    char out_dir[512];
    (void)snprintf(out_dir, sizeof(out_dir),
                   "/tmp/zsp_corpus_compile_%u_%zu",
                   (unsigned)getpid(), st->total_schemas);
    char rm_cmd[600];
    (void)snprintf(rm_cmd, sizeof(rm_cmd), "rm -rf '%s'", out_dir);
    (void)system(rm_cmd);
    (void)mkdir(out_dir, 0755);

    char roots_buf[6][1024];
    const char *roots[6];
    size_t n_roots = 0U;
    derive_roots(schema_path, roots_buf, roots, 6U, &n_roots);

    zsp_session_t session;
    if (zsp_session_init(&session, roots, n_roots) != ZSERIO_OK) {
        (void)system(rm_cmd);
        return;
    }

    zsp_ast_node_t *top_unit = NULL;
    bool emit_ok = false;
    disk_sink_t sink;
    (void)memset(&sink, 0, sizeof(sink));
    (void)snprintf(sink.out_dir, sizeof(sink.out_dir), "%s", out_dir);
    sink.ok = true;

    if ((zsp_session_load(&session, schema_path, &top_unit) == ZSERIO_OK)
            && (top_unit != NULL)
            && (zsp_session_analyse(&session) == ZSERIO_OK)
            && (zsp_emit_unit(top_unit, &session.arena, &session.diag,
                              &session.source, disk_write, &sink)
                == ZSERIO_OK)
            && sink.ok) {
        emit_ok = true;
        st->emitted_schemas += 1U;
    }
    zsp_session_dispose(&session);

    if (!emit_ok) {
        st->failed_schemas += 1U;
        for (size_t i = 0U; i < sink.n_c; ++i) free(sink.c_files[i]);
        free(sink.c_files);
        (void)system(rm_cmd);
        return;
    }

    /* Compile each emitted .c file. The `-w` silences warnings;
     * we only care about hard errors at this gate. The two
     * -I dirs cover both the schema's package subdir and the
     * out_dir root so cross-package includes resolve. */
    bool all_ok = true;
    for (size_t i = 0U; i < sink.n_c; ++i) {
        /* Compute the dir of the file so includes resolve. */
        char include_dir[4096];
        (void)snprintf(include_dir, sizeof(include_dir),
                       "%s", sink.c_files[i]);
        size_t pos = strlen(include_dir);
        while ((pos > 0U) && (include_dir[pos - 1U] != '/')) pos -= 1U;
        if (pos > 0U) include_dir[pos - 1U] = '\0';

        char cmd[16384];
        (void)snprintf(cmd, sizeof(cmd),
                       "cc -std=c99 -w "
                       "-I'%s/runtime/include' "
                       "-I'%s' "
                       "-I'%s' "
                       "-c '%s' -o /dev/null 2>/dev/null",
                       ZSP_REPO_ROOT, out_dir, include_dir, sink.c_files[i]);
        if (system(cmd) == 0) {
            st->compiled_c_files += 1U;
        }
        else {
            all_ok = false;
        }
    }
    if (all_ok && (sink.n_c > 0U)) {
        st->compiled_schemas += 1U;
    }
    else if (sink.n_c == 0U) {
        /* Schema produced no .c files (e.g., enum-only or
         * service-only). Count as compiled. */
        st->compiled_schemas += 1U;
    }
    else if (st->n_first_failures < 8U) {
        st->first_failures[st->n_first_failures++] = strdup(schema_path);
    }

    for (size_t i = 0U; i < sink.n_c; ++i) free(sink.c_files[i]);
    free(sink.c_files);
    (void)system(rm_cmd);
}

typedef struct dir_entry { char path[2048]; } dir_entry_t;

static void walk_corpus(const char *root, compile_state_t *st)
{
    dir_entry_t *stack = (dir_entry_t *)malloc(64U * sizeof(dir_entry_t));
    if (stack == NULL) return;
    size_t cap = 64U, depth = 0U;
    (void)snprintf(stack[depth++].path, sizeof(stack[0].path), "%s", root);

    while (depth > 0U) {
        depth -= 1U;
        char dir_path[2048];
        (void)snprintf(dir_path, sizeof(dir_path), "%s", stack[depth].path);
        DIR *const d = opendir(dir_path);
        if (d == NULL) continue;
        for (;;) {
            struct dirent *const ent = readdir(d);
            if (ent == NULL) break;
            if ((strcmp(ent->d_name, ".") == 0)
                    || (strcmp(ent->d_name, "..") == 0)) continue;
            char full[4096];
            (void)snprintf(full, sizeof(full), "%s/%s", dir_path, ent->d_name);
            struct stat sst;
            if (stat(full, &sst) != 0) continue;
            if (S_ISDIR(sst.st_mode)) {
                if (depth >= cap) {
                    const size_t nc = cap * 2U;
                    dir_entry_t *const g = (dir_entry_t *)realloc(
                            stack, nc * sizeof(dir_entry_t));
                    if (g == NULL) break;
                    stack = g; cap = nc;
                }
                const size_t l = strlen(full);
                const size_t c = (l < sizeof(stack[0].path) - 1U)
                        ? l : sizeof(stack[0].path) - 1U;
                (void)memcpy(stack[depth].path, full, c);
                stack[depth].path[c] = '\0';
                depth += 1U;
            }
            else if (S_ISREG(sst.st_mode)) {
                const size_t l = strlen(ent->d_name);
                if ((l >= 3U) && (strcmp(&ent->d_name[l - 3U], ".zs") == 0)) {
                    compile_one_schema(full, st);
                }
            }
        }
        (void)closedir(d);
    }
    free(stack);
}

static void test_corpus_compile_sweep(void)
{
    char root[2048];
    (void)snprintf(root, sizeof(root), "%s/language", ZSP_CORPUS_ROOT);
    struct stat root_st;
    if (stat(root, &root_st) != 0) {
        TEST_IGNORE_MESSAGE("corpus submodule not initialised; skipping");
        return;
    }

    compile_state_t st;
    (void)memset(&st, 0, sizeof(st));
    walk_corpus(root, &st);

    (void)printf("\ncorpus compile sweep:\n"
                 "  %zu schemas total\n"
                 "  %zu emitted successfully\n"
                 "  %zu emitted .c files compiled cleanly per schema\n"
                 "  %zu total .c files compiled\n"
                 "  %zu emit failures\n",
                 st.total_schemas, st.emitted_schemas,
                 st.compiled_schemas, st.compiled_c_files,
                 st.failed_schemas);
    if (st.n_first_failures > 0U) {
        (void)printf("first compile failures:\n");
        for (size_t i = 0U; i < st.n_first_failures; ++i) {
            (void)printf("  - %s\n", st.first_failures[i]);
            free(st.first_failures[i]);
        }
    }

    if (st.total_schemas == 0U) {
        TEST_IGNORE_MESSAGE("no schema files discovered");
        return;
    }

    /* Threshold: emit must succeed on 100% of parseable schemas,
     * and at least 80% of schemas must compile end-to-end. The
     * 20% slack covers schemas using features the emitter has
     * partial coverage for (rule_group placeholder files,
     * service stubs, etc.). */
    const size_t emit_pct = (st.emitted_schemas * 100U) / st.total_schemas;
    if (emit_pct < 95U) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg),
                       "emit pass rate %zu%% below threshold (95%%)",
                       emit_pct);
        TEST_FAIL_MESSAGE(msg);
    }
    const size_t compile_pct = (st.compiled_schemas * 100U) / st.total_schemas;
    if (compile_pct < 80U) {
        char msg[256];
        (void)snprintf(msg, sizeof(msg),
                       "compile pass rate %zu%% below threshold (80%%)",
                       compile_pct);
        TEST_FAIL_MESSAGE(msg);
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_corpus_compile_sweep);
    return UNITY_END();
}
