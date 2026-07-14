# Build integration

Wiring `zserio_c_gen` into a real build. Three patterns, ordered
by how much of your build system you want to give it.

## Getting the library

All routes build the runtime from source (there is no shared
library) and provide the same namespaced targets:
`zserio_c::runtime`, `zserio_c::parser`, and `zserio_c::gen`
(the generator executable).

```cmake
# Installed package (after `cmake --install`):
find_package(zserio_c 0.1 REQUIRED)

# Vendored checkout / git submodule (tests stay off automatically):
add_subdirectory(third_party/zserio-c)

# FetchContent:
include(FetchContent)
FetchContent_Declare(zserio_c
    GIT_REPOSITORY https://github.com/Klebert-Engineering/zserio-c.git
    GIT_TAG        v0.1.0)
FetchContent_MakeAvailable(zserio_c)
```

An installed tree also carries `lib/pkgconfig/zserio_c_runtime.pc`
for non-CMake builds. The snippets below use the plain target
names from an in-tree build; substitute the `zserio_c::` namespaced
forms when consuming any of the routes above.

## Pattern 1 — Pre-generate and check in

Run `zserio_c_gen` once outside the build, commit the generated
tree, build it like any other source.

```sh
build/parser/zserio_c_gen schemas/messages.zs -c gen
git add gen
```

Then in your build:

```cmake
add_library(messages STATIC
    gen/messages/foo.c
    gen/messages/bar.c
)
target_include_directories(messages PUBLIC
    gen
    ${ZSERIO_C_ROOT}/runtime/include
)
target_link_libraries(messages PUBLIC zserio_c_runtime)
```

**When to use it:** you want stable, reviewable build output;
your schemas change rarely; CI consumers shouldn't need the
generator.

**Trade-off:** schema and generated code drift if someone changes
one without re-running the generator. Add a CI job that
re-generates and diffs against the committed tree.

## Pattern 2 — CMake `add_custom_command`

Re-generate at build time when the schema changes.

```cmake
set(SCHEMA      ${CMAKE_CURRENT_SOURCE_DIR}/schemas/messages.zs)
set(GEN_DIR     ${CMAKE_CURRENT_BINARY_DIR}/zserio-gen)
set(GEN_OUTPUTS
    ${GEN_DIR}/messages/foo.h ${GEN_DIR}/messages/foo.c
    ${GEN_DIR}/messages/bar.h ${GEN_DIR}/messages/bar.c
)

add_custom_command(
    OUTPUT   ${GEN_OUTPUTS}
    COMMAND  zserio_c_gen ${SCHEMA} -c ${GEN_DIR}
    DEPENDS  ${SCHEMA} zserio_c_gen
    COMMENT  "zserio: generating C from ${SCHEMA}"
    VERBATIM
)

add_library(messages STATIC ${GEN_OUTPUTS})
target_include_directories(messages PUBLIC
    ${GEN_DIR}
    ${ZSERIO_C_ROOT}/runtime/include
)
target_link_libraries(messages PUBLIC zserio_c_runtime)
```

The output list has to be enumerable up front so CMake can wire
the dependency graph. For schemas where you don't know the output
filenames in advance, fall back to Pattern 3.

**When to use it:** schemas change during development and you
want incremental builds that respect schema changes.

**Trade-off:** you maintain the output-file list. The generator
exits 1 on first failure so a stale list breaks the build loud.

## Pattern 3 — Generate then glob

For schemas where you can't enumerate outputs (template-heavy
codebases, large package trees), run the generator at configure
time and glob the result.

```cmake
set(GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/zserio-gen)
file(MAKE_DIRECTORY ${GEN_DIR})

execute_process(
    COMMAND zserio_c_gen
        ${CMAKE_CURRENT_SOURCE_DIR}/schemas/messages.zs
        -c ${GEN_DIR}
    RESULT_VARIABLE rc
)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "zserio_c_gen failed (rc=${rc})")
endif()

file(GLOB_RECURSE GEN_SOURCES CONFIGURE_DEPENDS
    ${GEN_DIR}/*.c
    ${GEN_DIR}/*.h
)

add_library(messages STATIC ${GEN_SOURCES})
target_include_directories(messages PUBLIC
    ${GEN_DIR}
    ${ZSERIO_C_ROOT}/runtime/include
)
target_link_libraries(messages PUBLIC zserio_c_runtime)
```

`CONFIGURE_DEPENDS` makes the glob re-evaluate on each build —
without it, files added by a schema change won't be picked up
until you re-run cmake explicitly.

**When to use it:** large generated trees, NDS-style schemas with
hundreds of templated instantiations.

**Trade-off:** schema changes trigger a configure-time re-run; the
glob isn't perfectly tracked by Ninja.

## Plain Make

Same shape, different syntax:

```make
SCHEMA      := schemas/messages.zs
GEN_DIR     := build/gen
RUNTIME_INC := third_party/zserio-c/runtime/include
RUNTIME_SRC := third_party/zserio-c/runtime/src

GEN_STAMP   := $(GEN_DIR)/.stamp

$(GEN_STAMP): $(SCHEMA) | $(GEN_DIR)
	zserio_c_gen $< -c $(GEN_DIR)
	@touch $@

$(GEN_DIR):
	mkdir -p $@

# Pulling sources after the stamp ensures the glob runs after generation.
GEN_C_SOURCES = $(shell find $(GEN_DIR) -name '*.c' 2>/dev/null)
RT_C_SOURCES  = $(wildcard $(RUNTIME_SRC)/*.c)

app: app.c $(GEN_STAMP) $(GEN_C_SOURCES) $(RT_C_SOURCES)
	gcc -std=c99 -Wall -Wextra \
	    -I$(RUNTIME_INC) -I$(GEN_DIR) \
	    $(filter %.c,$^) -o $@
```

A stamp file plus a shell glob is the Make equivalent of CMake's
`CONFIGURE_DEPENDS`. Less precise than CMake's pattern 2 but
simpler if your schema's output list isn't known.

## Compiler flags

The generated code is C99 and clean under
`-Wall -Wextra -Wpedantic`. The CI build adds `-Werror` and
warns about format-truncation and strict-aliasing under `-O3`.

If you're integrating into a codebase with stricter warnings (e.g.
`-Wconversion`, `-Wsign-conversion`), the generated code passes
those too as of v1. The runtime is verified clean on:

- gcc 9–13 (`-std=c99 -Wall -Wextra -Wpedantic -Werror`)
- clang 12–17 (same flags)
- gcc 9 + UBSan + ASan
- clang 14 + UBSan + ASan + MSan

## Linking the runtime

The runtime is **header-only-by-convention** in shape but
implementation-in-`runtime/src/*.c` for size. There's no separate
shared library to install. You compile the runtime sources
alongside your generated code. The simplest CMake target:

```cmake
add_library(zserio_c_runtime STATIC
    ${ZSERIO_C_ROOT}/runtime/src/allocator.c
    ${ZSERIO_C_ROOT}/runtime/src/arena.c
    ${ZSERIO_C_ROOT}/runtime/src/bitstream.c
    ${ZSERIO_C_ROOT}/runtime/src/varint.c
    # …rest of runtime/src/*.c
)
target_include_directories(zserio_c_runtime PUBLIC
    ${ZSERIO_C_ROOT}/runtime/include
)
target_compile_features(zserio_c_runtime PUBLIC c_std_99)
```

The runtime is ~1500 LoC of C99 with no third-party deps. It
compiles in under a second on any modern host.

## Cross-compilation

The runtime has no host-only code paths. Cross-compiling for
ARM64, Cortex-M, RISC-V, etc. is the usual CMake toolchain-file
exercise. The runtime makes no syscalls and never touches the
filesystem; the only libc dependency is the allocator backing
(if you're using `zserio_allocator_libc`) and `memcpy` / `memset`,
both of which freestanding implementations provide.

The generator (`zserio_c_gen`) is a host build artifact — you do
not cross-compile it for the target. Build it on the host once,
run it during the cross-build to produce target sources.

## Reproducible builds

`zserio_c_gen` is deterministic — same inputs, byte-identical
outputs across runs, hosts, and compiler versions. There are no
timestamps, no random identifiers, no `__DATE__` / `__TIME__` in
the output. If you're chasing bit-for-bit reproducible builds for
SBOM purposes, the generator is well-behaved.

## See also

- [`zserio_c_gen` CLI reference](../cli.md) — flag-by-flag.
- [Allocators](allocators.md) — link `zserio_allocator_libc` for
  development, swap to an arena for the target.
- [Getting started](../getting-started.md) — the smallest
  working build.
