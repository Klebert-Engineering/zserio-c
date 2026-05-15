# Build the embedded-decode example as a standalone executable.
#
# Usage:
#   cmake -B build -S .
#   cmake --build build
#   ./build/embedded_decode
#
# This re-runs the generator via the project's `build_generator_jar.sh`
# helper if the generated `gen/` directory is missing. Pre-generating
# is also fine — re-running is idempotent.

cmake_minimum_required(VERSION 3.16)
project(zserio_c_embedded_decode_example C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
set(EXAMPLE_GEN "${CMAKE_CURRENT_SOURCE_DIR}/gen")

# Codegen: invoke the generator if the gen/ directory is empty. The
# integrator-friendly path is to pre-generate and check the output
# in — committed `gen/` makes the build hermetic for downstream
# consumers without a Java toolchain.
if(NOT EXISTS "${EXAMPLE_GEN}/telemetry/frame.c")
    find_package(Java COMPONENTS Runtime REQUIRED)
    set(JARS
        "${REPO_ROOT}/build-jars/zserio_core.jar"
        "${REPO_ROOT}/generator/build/jar/zserio_c.jar"
        "${REPO_ROOT}/build-jars/zserio.jar"
    )
    string(REPLACE ";" ":" JAR_CP "${JARS}")
    execute_process(
        COMMAND "${Java_JAVA_EXECUTABLE}" -cp "${JAR_CP}"
                zserio.tools.ZserioTool
                -src "${CMAKE_CURRENT_SOURCE_DIR}" telemetry.zs
                -c "${EXAMPLE_GEN}"
        RESULT_VARIABLE _gen_rc
    )
    if(NOT _gen_rc EQUAL 0)
        message(FATAL_ERROR "zserio-c codegen failed (rc=${_gen_rc})")
    endif()
endif()

file(GLOB EXAMPLE_GEN_SOURCES "${EXAMPLE_GEN}/telemetry/*.c")
file(GLOB RUNTIME_SOURCES "${REPO_ROOT}/runtime/src/*.c")

add_executable(embedded_decode
    main.c
    ${EXAMPLE_GEN_SOURCES}
    ${RUNTIME_SOURCES}
)
target_include_directories(embedded_decode PRIVATE
    "${REPO_ROOT}/runtime/include"
    "${EXAMPLE_GEN}"
)
target_compile_options(embedded_decode PRIVATE
    -Wall -Wextra -Wpedantic
)
