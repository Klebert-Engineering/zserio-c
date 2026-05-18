# tests/ — top-level test entry point.
#
# Per-area test directories add themselves below. Each is built only
# when its own CMakeLists.txt exists, so partial trees configure cleanly.

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/runtime/CMakeLists.txt")
    add_subdirectory(runtime)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/parser/CMakeLists.txt")
    add_subdirectory(parser)
endif()
