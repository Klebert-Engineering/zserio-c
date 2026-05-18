# parser/ — libzserio_c_parser
#
# The C-native parser library. Phase 7 deliverable: replaces the
# Java front-end (zserio_core.jar + generator/zserio_c.jar) with a
# pure C99 toolchain.
#
# This first slice covers the foundation infrastructure:
#   - source manager (file table + lazy line-start lookup)
#   - diagnostic collector + gcc-style / JSON formatters
#
# Lexer + AST + recursive-descent parser arrive in subsequent
# slices per the milestones in docs/phase-7/scope.md.

add_library(zserio_c_parser STATIC
    src/source.c
    src/diag.c
    src/token.c
    src/lexer.c
    src/arena.c
    src/ast.c
    src/parser.c
    src/sema.c
    src/emit.c
    src/session.c
)

target_include_directories(zserio_c_parser
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# The parser depends on the runtime's allocator + status headers.
# Public link because consumers of <zsparser/*.h> see those types
# in our public API.
target_link_libraries(zserio_c_parser PUBLIC zserio_c::runtime)

target_compile_features(zserio_c_parser PUBLIC c_std_99)

set_target_properties(zserio_c_parser PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

add_library(zserio_c::parser ALIAS zserio_c_parser)

# CLI driver: zserio-c-gen <input.zs> -c <output-dir>
add_executable(zserio_c_gen cli/main.c)
target_link_libraries(zserio_c_gen PRIVATE zserio_c::parser)
target_compile_features(zserio_c_gen PRIVATE c_std_99)
