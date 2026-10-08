add_executable(joltfmt src/main.c)
target_link_libraries(joltfmt PRIVATE tilly)
target_include_directories(joltfmt PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
