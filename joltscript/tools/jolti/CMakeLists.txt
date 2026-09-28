add_executable(jolti src/main.c)
target_link_libraries(jolti PRIVATE joltscript_glue joltscript_execution)
target_include_directories(jolti PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(jolti PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
