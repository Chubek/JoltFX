add_executable(joltdoc src/main.c)
target_include_directories(joltdoc PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
