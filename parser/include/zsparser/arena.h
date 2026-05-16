/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSPARSER_ARENA_H_
#define ZSPARSER_ARENA_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "zserio/allocator.h"
#include "zserio/status.h"

/**
 * @file
 * Chunked bump arena for the parser. The runtime's
 * `zserio_arena_t` requires a single fixed-size backing buffer
 * supplied by the caller; that shape is right for the
 * decode-time path but not for the parser, which doesn't know
 * up front how much memory the AST will need.
 *
 * Allocator behaviour:
 *   - Each `zsp_arena_alloc(a, size, align)` bumps `used` in the
 *     current chunk and returns the aligned pointer.
 *   - On chunk exhaustion a new chunk is allocated through the
 *     underlying `zserio_allocator_t`. Chunks form a singly-linked
 *     list rooted at `first`.
 *   - There is no per-allocation `free`. The whole arena is
 *     released in O(chunks) by `zsp_arena_dispose`.
 *   - Allocations larger than `ZSP_ARENA_CHUNK_BYTES` get their
 *     own dedicated chunk; no waste from oversize requests.
 *
 * This is the standard "obstack / bump arena" shape used by
 * lua, clang, and most production compiler frontends.
 */

#define ZSP_ARENA_CHUNK_BYTES ((size_t)(64U * 1024U))

typedef struct zsp_arena_chunk {
    struct zsp_arena_chunk *next;
    uint8_t                *bytes;       /**< Points just past this header. */
    size_t                  cap;
    size_t                  used;
    size_t                  alloc_size;  /**< For dispose-time `free`. */
} zsp_arena_chunk_t;

typedef struct zsp_arena {
    const zserio_allocator_t *alloc;
    zsp_arena_chunk_t        *first;
    zsp_arena_chunk_t        *current;
    size_t                    bytes_used;   /**< Cumulative; diagnostics. */
    size_t                    bytes_total;  /**< Cumulative chunk cap. */
} zsp_arena_t;

zserio_status_t zsp_arena_init(zsp_arena_t *a,
                               const zserio_allocator_t *alloc);

void zsp_arena_dispose(zsp_arena_t *a);

/**
 * Allocate `size` bytes aligned to `align` (a power of two).
 * Returns NULL on out-of-memory; the underlying allocator's
 * failure propagates as a NULL return. The returned region is
 * zero-initialised.
 */
void *zsp_arena_alloc(zsp_arena_t *a, size_t size, size_t align);

/**
 * Copy `len` bytes from `src` into the arena, terminating the
 * copy with a NUL byte (so it is usable as a C string). Returns
 * NULL on OOM. Useful for stashing identifier spellings the
 * AST will outlive the source for (rare, but supported).
 */
char *zsp_arena_strdup(zsp_arena_t *a, const char *src, size_t len);

#endif /* ZSPARSER_ARENA_H_ */
