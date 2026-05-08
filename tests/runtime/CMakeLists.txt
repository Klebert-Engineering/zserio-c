# tests/runtime/ — runtime unit tests, Unity-driven.

include(FetchContent)

# Vendor Unity at a pinned tag. The downloaded tree lives under the
# build directory; nothing is added to source control by FetchContent.
FetchContent_Declare(
    unity
    GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(unity)

# Unity is third-party code; do not apply our strict warnings to it.
# It would otherwise fail under -Wfloat-equal -Werror inherited from
# the top-level add_compile_options.
if(TARGET unity)
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(unity PRIVATE -w)
    endif()
endif()

function(zserio_c_add_runtime_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE zserio_c::runtime unity)
    target_compile_features(${name} PRIVATE c_std_99)
    # Unity has its own warnings stance; we don't apply our strict
    # flags to it. Our own test sources still inherit the top-level
    # warning flags.
    add_test(NAME ${name} COMMAND ${name})
endfunction()

zserio_c_add_runtime_test(test_allocator)
zserio_c_add_runtime_test(test_bitstream)
zserio_c_add_runtime_test(test_bytes)
zserio_c_add_runtime_test(test_delta)
zserio_c_add_runtime_test(test_extern)
zserio_c_add_runtime_test(test_float)
zserio_c_add_runtime_test(test_primitives)
zserio_c_add_runtime_test(test_string)
zserio_c_add_runtime_test(test_varint)
