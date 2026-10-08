add_executable(joltdoc src/main.c)
target_link_libraries(joltdoc PRIVATE tilly)
target_include_directories(joltdoc PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
