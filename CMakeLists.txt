cmake_minimum_required(VERSION 3.20)

project(zserio-c
    VERSION 0.1.0
    DESCRIPTION "ANSI C port of the zserio schema IDL, designed for ISO 26262 / MISRA C:2012"
    LANGUAGES C
)

# Wire-format and schema-language are independently versioned axes
# (REQ-DM-007 / docs/spec/compatibility.md). These remain stable
# regardless of the project's release version.
set(ZSERIO_WIRE_FORMAT_VERSION "1.0")
set(ZSERIO_SCHEMA_LANGUAGE_VERSION "1.0")
set(ZSERIO_UPSTREAM_PIN "v2.18.1")

# C99 strict, no compiler extensions. MISRA C:2012 baseline.
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Export compile_commands.json so clang-tidy / cppcheck / IDEs can find it.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Tests and install rules default ON only when zserio-c is the
# top-level project: consumers embedding it via add_subdirectory /
# FetchContent get just the library targets (no Unity fetch, no
# install pollution) unless they opt in.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(ZSERIO_C_TOP_LEVEL ON)
else()
    set(ZSERIO_C_TOP_LEVEL OFF)
endif()

option(ZSERIO_C_BUILD_TESTS    "Build the test suite"               ${ZSERIO_C_TOP_LEVEL})
option(ZSERIO_C_BUILD_EXAMPLES "Build example programs"             OFF)
option(ZSERIO_C_COVERAGE       "Instrument for line+branch coverage" OFF)
option(ZSERIO_C_WERROR         "Treat warnings as errors"           ON)
option(ZSERIO_C_INSTALL        "Generate install and find_package rules" ${ZSERIO_C_TOP_LEVEL})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wstrict-prototypes -Wmissing-prototypes
        -Wpointer-arith -Wcast-qual -Wcast-align
        -Wundef -Wwrite-strings -Wformat=2
        -Wstrict-overflow=5 -Wfloat-equal
    )
    if(ZSERIO_C_WERROR)
        add_compile_options(-Werror)
    endif()
    if(ZSERIO_C_COVERAGE)
        add_compile_options(--coverage -O0 -g)
        add_link_options(--coverage)
    endif()
endif()

# Subdirectories are added only if they contain a CMakeLists.txt,
# so partial checkouts and out-of-tree harness builds configure
# cleanly.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/CMakeLists.txt")
    add_subdirectory(runtime)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/parser/CMakeLists.txt")
    add_subdirectory(parser)
endif()

if(ZSERIO_C_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()

if(ZSERIO_C_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# --- Install / find_package support ---------------------------------------
# `find_package(zserio_c <version>)` after `cmake --install` imports
# zserio_c::runtime, zserio_c::parser, and the zserio_c::gen
# generator executable. Version compatibility is SameMinorVersion:
# pre-1.0, minor bumps may change APIs (docs/spec/compatibility.md).
if(ZSERIO_C_INSTALL)
    include(GNUInstallDirs)
    include(CMakePackageConfigHelpers)

    install(EXPORT zserio_c-targets
        FILE zserio_c-targets.cmake
        NAMESPACE zserio_c::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zserio_c
    )

    configure_package_config_file(
        cmake/zserio_c-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/zserio_c-config.cmake
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zserio_c
    )
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/zserio_c-config-version.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/zserio_c-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/zserio_c-config-version.cmake
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/zserio_c
    )

    # pkg-config for non-CMake build systems (runtime library only —
    # that is what shipped binaries link).
    configure_file(cmake/zserio_c_runtime.pc.in
        ${CMAKE_CURRENT_BINARY_DIR}/zserio_c_runtime.pc @ONLY)
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/zserio_c_runtime.pc
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endif()
