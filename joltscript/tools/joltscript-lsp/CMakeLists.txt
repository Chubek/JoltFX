add_executable(joltscript-lsp src/main.c)
target_link_libraries(joltscript-lsp PRIVATE joltscript_glue joltscript_execution)
target_include_directories(joltscript-lsp PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(joltscript-lsp PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
