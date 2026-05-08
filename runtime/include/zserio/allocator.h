/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Fabian Klebert
 */

#ifndef ZSERIO_ALLOCATOR_H_
#define ZSERIO_ALLOCATOR_H_

#include <stddef.h>

/**
 * @file
 * Allocator vtable for the zserio-c runtime.
 *
 * All dynamic memory used by the runtime and by code emitted by the
 * zserio-c generator flows through this allocator. The runtime never
 * calls `malloc`, `calloc`, `realloc`, or `free` (or any equivalent)
 * directly; see REQ-FS-001.
 *
 * The allocator is a pair of pointers: a vtable describing the
 * allocation operations, and an opaque `userdata` passed back to every
 * callback so the allocator implementation can locate its own state.
 *
 * No global default exists; every public API call that may allocate
 * takes a `zserio_allocator_t` pointer explicitly. See REQ-FS-007.
 */

/**
 * Function-pointer table describing an allocator's operations.
 *
 * Instances SHOULD live in `.rodata` (declared `static const`) so the
 * dispatch table itself is immutable.
 */
typedef struct zserio_allocator_vtable {
    /**
     * Allocate at least `size` bytes of writable memory, aligned to
     * at least `alignment` bytes. Returns a non-NULL pointer on
     * success, NULL on out-of-memory.
     *
     * `size` MUST be greater than zero. `alignment` MUST be a power
     * of two. Behavior is undefined for invalid arguments; the
     * runtime validates both at the public API boundary before
     * invoking any allocator callback (REQ-FS-006).
     *
     * MUST NOT be NULL on a usable allocator.
     */
    void *(*alloc)(void *userdata, size_t size, size_t alignment);

    /**
     * Resize the allocation at `ptr` from `old_size` to `new_size`
     * bytes, preserving the smaller of the two and preserving
     * `alignment`. Returns the new (possibly relocated) pointer on
     * success, NULL on out-of-memory.
     *
     * On failure (NULL return), the original block at `ptr` remains
     * valid and unchanged (POSIX-`realloc` semantics).
     *
     * `ptr` MUST have been returned by a prior `alloc` or `realloc`
     * call on the same allocator. `old_size` and `alignment` MUST
     * match the values passed at allocation time.
     *
     * MAY be NULL. When NULL, the runtime falls back to `alloc` +
     * `memcpy` + `free`. Allocators that can resize in place SHOULD
     * provide this for the buffer-growth fast path during decode.
     */
    void *(*realloc)(void *userdata, void *ptr,
                     size_t old_size, size_t new_size, size_t alignment);

    /**
     * Free a previously-allocated block.
     *
     * `ptr` MAY be NULL; the call is a no-op in that case. Otherwise
     * `ptr` MUST have been returned by a prior `alloc` or `realloc`
     * call on the same allocator, and `size` and `alignment` MUST
     * match the values passed at allocation time. The runtime
     * threads these values through every owning structure to make
     * this contract callable.
     *
     * MUST NOT be NULL on a usable allocator.
     */
    void (*free)(void *userdata, void *ptr, size_t size, size_t alignment);
} zserio_allocator_vtable_t;

/**
 * Caller-passed allocator handle.
 *
 * Carries a vtable pointer and an opaque userdata pointer. The pair
 * fully describes an allocator to the runtime. Both pointers MUST
 * remain valid for the lifetime of any object the allocator has
 * produced.
 */
typedef struct zserio_allocator {
    const zserio_allocator_vtable_t *vtable;   /**< MUST NOT be NULL on call. */
    void                            *userdata; /**< Opaque; passed back to every callback. */
} zserio_allocator_t;

/**
 * Returns an allocator backed by the C standard library's
 * `malloc` / `realloc` / `free`.
 *
 * The returned value is stateless and safe to copy. It is NOT a
 * global default — explicit construction is required at every
 * public API call. This is by design: the runtime never carries a
 * hidden allocator (REQ-FS-007).
 *
 * On hosts where a `malloc`-backed allocator is unsuitable (typical
 * automotive ECU builds), callers construct their own
 * `zserio_allocator_t` referencing a static-pool or arena
 * implementation.
 */
zserio_allocator_t zserio_allocator_libc(void);

#endif /* ZSERIO_ALLOCATOR_H_ */
