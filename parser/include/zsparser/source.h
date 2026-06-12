/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Klebert Engineering
 */

#ifndef ZSPARSER_SOURCE_H_
#define ZSPARSER_SOURCE_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/status.h"

/**
 * @file
 * Source manager for the zserio-c parser.
 *
 * Owns the source bytes for every file the compilation session
 * touches. Hands out u16 file identifiers that the lexer and
 * parser stamp on every token and AST node alongside a u32 byte
 * offset; this `(file_id, offset)` pair is the Q6 decision recorded
 * in `docs/design/parser.md`.
 *
 * Line and column information is computed lazily on first
 * diagnostic emission via a per-file line-start table — most
 * compiles succeed with zero diagnostics and pay nothing for line
 * tracking.
 *
 * All dynamic memory flows through the caller-supplied
 * `zserio_allocator_t`. The source manager performs no filesystem
 * I/O of its own: callers load file bytes (typically via libc
 * `fopen`/`fread` in the driver layer) and register the resulting
 * buffer with `zsp_source_add`. Decoupling I/O from the parser
 * keeps the parser library free of OS-specific concerns.
 */

typedef uint16_t zsp_file_id_t;

#define ZSP_FILE_ID_INVALID  ((zsp_file_id_t)0xFFFFU)
#define ZSP_SOURCE_MAX_FILES ((size_t)0xFFFEU)   /* 65534 valid IDs */
#define ZSP_SOURCE_INITIAL_CAP ((size_t)16U)

/**
 * Per-file metadata, owned by the source manager. The fields are
 * exposed via `zsp_source_get` so the lexer / parser can iterate
 * over a file's bytes without indirection. Treated as read-only
 * by every consumer other than the source manager itself.
 *
 * Ownership is tracked by separate non-const shadow pointers
 * (`name_owned` / `bytes_owned`). When non-NULL, dispose frees
 * them via the allocator. This avoids casting away `const` on
 * dispose (MISRA Rule 11.8): the shadow pointers are non-const
 * by construction because the corresponding registration call
 * takes a `char *` / `uint8_t *` to transfer ownership.
 */
typedef struct zsp_file {
    const char    *name;          /**< Display name, e.g. "foo.zs". */
    size_t         name_len;
    const uint8_t *bytes;         /**< File contents. */
    size_t         len;

    /* Owning shadow pointers, NULL if registration was borrowed. */
    char    *name_owned;
    size_t   name_alloc_size;
    uint8_t *bytes_owned;
    size_t   bytes_alloc_size;

    /* Lazy line-start table. line_starts[i] is the byte offset
     * where line (i + 1) begins. line_starts[0] is always 0.
     * NULL until the first zsp_source_line_col / line_text call. */
    uint32_t *line_starts;
    size_t    line_count;
    size_t    line_cap;
} zsp_file_t;

/**
 * Compilation-session-scoped source manager. Owns a growable
 * array of `zsp_file_t` plus everything those entries point at
 * when `owns_*` flags are true.
 *
 * Not thread-safe. A single compilation session holds one source
 * manager; concurrent compilations get distinct ones.
 */
typedef struct zsp_source {
    const zserio_allocator_t *alloc;
    zsp_file_t               *files;
    size_t                    count;
    size_t                    cap;
} zsp_source_t;

/**
 * Initialize an empty source manager backed by `alloc`. `alloc`
 * MUST outlive the source manager.
 *
 * Returns ZSERIO_OK on success, ZSERIO_ERR_INVALID_ARGUMENT if
 * either argument is NULL or the allocator's vtable is invalid,
 * ZSERIO_ERR_OUT_OF_MEMORY if the initial array allocation fails.
 */
zserio_status_t zsp_source_init(zsp_source_t *src,
                                const zserio_allocator_t *alloc);

/**
 * Release every owned resource held by the source manager. After
 * the call returns, `src` is back to its zero-initialized state
 * and may be re-initialized via `zsp_source_init`.
 *
 * Safe to call on a partially-initialized source manager (e.g. if
 * a prior `zsp_source_init` returned an error mid-way).
 */
void zsp_source_dispose(zsp_source_t *src);

/**
 * Register a source buffer that the caller continues to own.
 *
 * The source manager stores the pointers as-is; the caller MUST
 * keep `name` and `bytes` alive for the lifetime of the source
 * manager.
 *
 * On success, the file's identifier is written to `*out_id`.
 *
 * Failure modes:
 *   - ZSERIO_ERR_INVALID_ARGUMENT — NULL pointer or zero-length name
 *   - ZSERIO_ERR_OUT_OF_MEMORY    — array growth failed, or
 *                                   `ZSP_SOURCE_MAX_FILES` reached
 */
zserio_status_t zsp_source_add_borrowed(
    zsp_source_t *src,
    const char    *name, size_t name_len,
    const uint8_t *bytes, size_t bytes_len,
    zsp_file_id_t *out_id);

/**
 * Register a source buffer whose ownership transfers to the
 * source manager. `name` and `bytes` MUST have been obtained from
 * the same allocator passed to `zsp_source_init` and will be
 * released in `zsp_source_dispose` via that allocator's `free`
 * callback at the original allocation sizes (`name_alloc_size`,
 * `bytes_alloc_size`).
 *
 * Taking non-const pointers makes the ownership transfer explicit
 * at the API boundary, which sidesteps Rule 11.8 entirely.
 *
 * The `name` buffer must hold at least `name_len + 1` bytes
 * (the trailing NUL is not required to be set, but the allocation
 * size includes the slot for it for symmetry with C string
 * conventions).
 */
zserio_status_t zsp_source_add_owned(
    zsp_source_t *src,
    char    *name,  size_t name_len,  size_t name_alloc_size,
    uint8_t *bytes, size_t bytes_len, size_t bytes_alloc_size,
    zsp_file_id_t *out_id);

/**
 * Look up the file entry for a given id.
 *
 * Returns true and writes `*out` on success; returns false and
 * leaves `*out` untouched if the id is invalid (out of range or
 * INVALID).
 */
bool zsp_source_get(const zsp_source_t *src, zsp_file_id_t id,
                    const zsp_file_t **out);

/**
 * Recover (line, col) — both 1-based — for a byte offset inside
 * the given file. Builds the per-file line-start table on first
 * call.
 *
 * `out_line` and `out_col` may individually be NULL to skip
 * computing that result. The offset is clamped to the file's
 * length so callers may pass EOF positions safely.
 *
 * Returns ZSERIO_ERR_INVALID_ARGUMENT if `id` is invalid;
 * ZSERIO_ERR_OUT_OF_MEMORY if line-start-table allocation fails.
 */
zserio_status_t zsp_source_line_col(
    zsp_source_t *src, zsp_file_id_t id, uint32_t offset,
    uint32_t *out_line, uint32_t *out_col);

/**
 * Retrieve the byte slice covering a 1-based source line, for
 * carat-pointed snippet rendering. The returned slice does NOT
 * include the trailing newline.
 *
 * Builds the line-start table on first call. Returns
 * ZSERIO_ERR_VALUE_OUT_OF_RANGE if `line` is 0 or beyond the
 * file's line count.
 */
zserio_status_t zsp_source_line_text(
    zsp_source_t *src, zsp_file_id_t id, uint32_t line,
    const uint8_t **out_bytes, size_t *out_len);

#endif /* ZSPARSER_SOURCE_H_ */
