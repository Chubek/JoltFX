add_executable(joltc src/main.c)
target_link_libraries(joltc PRIVATE joltscript_glue joltscript_execution)
target_include_directories(joltc PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(joltc PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
