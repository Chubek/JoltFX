add_library(joltscript_execution STATIC
    src/vm.c
    src/bytecode.c
    src/pipeline.c
)

target_include_directories(joltscript_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(joltscript_execution PUBLIC tilly)

set_project_warnings(joltscript_execution)

if(NOT MSVC)
    target_link_libraries(joltscript_execution PUBLIC m)
endif()
