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
