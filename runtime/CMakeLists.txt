# runtime/ — libzserio_c_runtime
#
# The C runtime library that generated code links against.
# Public headers in include/zserio/, implementations in src/.

add_library(zserio_c_runtime STATIC
    src/allocator.c
    src/bitstream.c
    src/bytes.c
    src/delta.c
    src/extern.c
    src/float.c
    src/primitives.c
    src/string.c
    src/varint.c
)

target_include_directories(zserio_c_runtime
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# C99 strict for our own sources (top-level already sets this; restated
# here so the target carries the property when used as a subdirectory).
target_compile_features(zserio_c_runtime PUBLIC c_std_99)

set_target_properties(zserio_c_runtime PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Convenience alias mirroring the typical "namespace::target" idiom.
add_library(zserio_c::runtime ALIAS zserio_c_runtime)
