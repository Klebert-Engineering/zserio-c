/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#include "zserio/allocator.h"

#include <stdlib.h>

/*
 * libc-backed allocator: the only translation unit in the runtime
 * permitted to call malloc / realloc / free directly. Every other
 * runtime / generated source goes through the zserio_allocator_t
 * vtable (REQ-FS-001).
 *
 * The allocator is stateless. `userdata` is unused. Two instances
 * returned from zserio_allocator_libc() are interchangeable.
 *
 * Alignment handling matches upstream pmr/NewDeleteResource: the
 * `alignment` argument is currently ignored. malloc returns memory
 * aligned to alignof(max_align_t), which satisfies all alignment
 * requirements of the wire-format primitives. Callers needing
 * stricter alignment plug their own allocator.
 */

static void *libc_alloc(void *userdata, size_t size, size_t alignment)
{
    (void)userdata;
    (void)alignment;
    /* size > 0 is a precondition of zserio_allocator_t.alloc; the
     * runtime validates at the public boundary (REQ-FS-006). */
    return malloc(size);
}

static void *libc_realloc(void *userdata, void *ptr,
                          size_t old_size, size_t new_size, size_t alignment)
{
    (void)userdata;
    (void)old_size;
    (void)alignment;
    /* On failure realloc preserves the original block, matching the
     * contract documented in allocator.h. */
    return realloc(ptr, new_size);
}

static void libc_free(void *userdata, void *ptr, size_t size, size_t alignment)
{
    (void)userdata;
    (void)size;
    (void)alignment;
    free(ptr);
}

static const zserio_allocator_vtable_t LIBC_VTABLE = {
    libc_alloc,
    libc_realloc,
    libc_free,
};

zserio_allocator_t zserio_allocator_libc(void)
{
    const zserio_allocator_t a = {
        &LIBC_VTABLE,
        NULL, /* userdata: unused */
    };
    return a;
}
