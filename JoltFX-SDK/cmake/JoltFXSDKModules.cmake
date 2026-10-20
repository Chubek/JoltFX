# Header/service-table modules deliberately do not link the engine.
function(joltfx_sdk_add_plugin target)
    add_library(${target} MODULE ${ARGN})
    target_link_libraries(${target} PRIVATE JoltFXSDK::plugins)
    set_target_properties(${target} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
        C_STANDARD 11 C_STANDARD_REQUIRED ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    # Clang's instrumented modules resolve sanitizer runtime symbols from the
    # instrumented executable. --no-undefined would reject those valid imports.
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT JFX_ASAN AND NOT JFX_UBSAN
       AND NOT "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS}" MATCHES "-fsanitize")
        target_link_options(${target} PRIVATE -Wl,--no-undefined)
    endif()
endfunction()
function(joltfx_sdk_add_renderer target)
    add_library(${target} MODULE ${ARGN})
    target_link_libraries(${target} PRIVATE JoltFXSDK::renderer)
    set_target_properties(${target} PROPERTIES PREFIX "" C_VISIBILITY_PRESET hidden
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON
        C_STANDARD 11 C_STANDARD_REQUIRED ON CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT JFX_ASAN AND NOT JFX_UBSAN
       AND NOT "${CMAKE_C_FLAGS} ${CMAKE_CXX_FLAGS} ${CMAKE_MODULE_LINKER_FLAGS}" MATCHES "-fsanitize")
        target_link_options(${target} PRIVATE -Wl,--no-undefined)
    endif()
endfunction()
