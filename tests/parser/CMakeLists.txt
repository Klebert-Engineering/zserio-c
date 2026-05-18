# parser/tests — parser unit tests, Unity-driven.
#
# The Unity dependency is fetched once by tests/runtime; we reuse
# the same imported target here.

function(zserio_c_add_parser_test name)
    add_executable(${name} ${name}.c)
    target_link_libraries(${name} PRIVATE zserio_c::parser unity)
    target_compile_features(${name} PRIVATE c_std_99)
    add_test(NAME ${name} COMMAND ${name})
endfunction()

zserio_c_add_parser_test(test_source)
zserio_c_add_parser_test(test_diag)
zserio_c_add_parser_test(test_lexer)
zserio_c_add_parser_test(test_arena_ast)
zserio_c_add_parser_test(test_parser)
zserio_c_add_parser_test(test_sema)
zserio_c_add_parser_test(test_emit)

zserio_c_add_parser_test(test_emit_compile)
zserio_c_add_parser_test(test_session)

zserio_c_add_parser_test(test_corpus_compile)
target_compile_definitions(test_corpus_compile PRIVATE
    "ZSP_CORPUS_ROOT=\"${CMAKE_SOURCE_DIR}/third_party/zserio-test-data\""
    "ZSP_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)
target_compile_definitions(test_emit_compile PRIVATE
    "ZSP_REPO_ROOT=\"${CMAKE_SOURCE_DIR}\""
)

# Acceptance test: parses real upstream schemas from
# third_party/zserio-test-data. Tests gracefully skip when the
# submodule is uninitialised.
zserio_c_add_parser_test(test_acceptance)
target_compile_definitions(test_acceptance PRIVATE
    "ZSP_CORPUS_ROOT=\"${CMAKE_SOURCE_DIR}/third_party/zserio-test-data\""
)

zserio_c_add_parser_test(test_corpus_sweep)
target_compile_definitions(test_corpus_sweep PRIVATE
    "ZSP_CORPUS_ROOT=\"${CMAKE_SOURCE_DIR}/third_party/zserio-test-data\""
)
