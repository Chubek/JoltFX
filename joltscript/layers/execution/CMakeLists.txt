add_library(joltscript_execution STATIC
    src/vm.c
    src/bytecode.c
    src/pipeline.c
    src/image_task.c
    src/audio_task.c
)

target_include_directories(joltscript_execution PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(joltscript_execution PUBLIC tilly)
# The vendored klib headers are included as SYSTEM so their macros (notably the
# khash initialiser, which narrows size_t to khint32_t) do not raise this
# project's -Wconversion in every translation unit that uses them.
target_include_directories(joltscript_execution SYSTEM PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/third_party/klib>)

set_project_warnings(joltscript_execution)

if(NOT MSVC)
    target_link_libraries(joltscript_execution PUBLIC m)
endif()
