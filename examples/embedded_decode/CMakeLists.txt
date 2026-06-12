# Build the embedded-decode example.
#
# Two supported modes:
#   1. Standalone (the integrator pattern — vendor the runtime,
#      build from source):
#        cmake -B build -S .
#        cmake --build build
#        ./build/embedded_decode
#   2. As part of the parent build via -DZSERIO_C_BUILD_EXAMPLES=ON,
#      where it links the parent's zserio_c::runtime target.
#
# The generated code under gen/ is committed so the build is
# hermetic — no generator run needed. To regenerate after editing
# telemetry.zs, run the C-native generator from the repo root:
#
#   cmake --preset gcc-release && cmake --build --preset gcc-release --target zserio_c_gen
#   ./build/gcc-release/parser/zserio_c_gen telemetry.zs -c gen
#
# (run from this directory; see docs/cli.md for the flag reference)

if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/gen/telemetry/frame.c")
    message(FATAL_ERROR
        "examples/embedded_decode/gen/ is missing its generated code. "
        "Regenerate with zserio_c_gen — see the comment header of this file.")
endif()

file(GLOB EXAMPLE_GEN_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/gen/telemetry/*.c")

if(NOT CMAKE_PROJECT_NAME STREQUAL "zserio-c")
    # Standalone mode: compile the runtime sources directly.
    cmake_minimum_required(VERSION 3.16)
    project(zserio_c_embedded_decode_example C)

    set(CMAKE_C_STANDARD 99)
    set(CMAKE_C_STANDARD_REQUIRED ON)
    set(CMAKE_C_EXTENSIONS OFF)

    set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
    file(GLOB RUNTIME_SOURCES "${REPO_ROOT}/runtime/src/*.c")

    add_executable(embedded_decode
        main.c
        ${EXAMPLE_GEN_SOURCES}
        ${RUNTIME_SOURCES}
    )
    target_include_directories(embedded_decode PRIVATE
        "${REPO_ROOT}/runtime/include"
        "${CMAKE_CURRENT_SOURCE_DIR}/gen"
    )
    target_compile_options(embedded_decode PRIVATE
        -Wall -Wextra -Wpedantic
    )
else()
    # Parent-build mode: link the runtime target.
    add_executable(embedded_decode
        main.c
        ${EXAMPLE_GEN_SOURCES}
    )
    target_include_directories(embedded_decode PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/gen"
    )
    target_link_libraries(embedded_decode PRIVATE zserio_c::runtime)
endif()
