# Applies the project's warning set to a target.
#
# The flag list is chosen per language, not per project: this repository builds
# C11 and C++17, and a toolchain can legitimately use different compilers for
# the two (for example GCC for C and Clang for C++). Selecting on
# CMAKE_C_COMPILER_ID alone left C++ targets with no warnings at all under such
# a toolchain, and passed Clang-only flags to MSVC.

set(JFX_CLANG_WARNINGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wcast-align
    -Wunused
    -Wconversion
    -Wsign-conversion
    -Wnull-dereference
    -Wdouble-promotion
    -Wformat=2
)

# Clang-only extras that matter for the C++ sources.
set(JFX_CXX_CLANG_WARNINGS
    -Wnon-virtual-dtor
    -Woverloaded-virtual
)

set(JFX_MSVC_WARNINGS
    /W4
    /w14640
    /permissive-
)

function(_jfx_select_warnings language_id out_var)
    if(language_id MATCHES ".*Clang")
        set(${out_var} ${JFX_CLANG_WARNINGS} PARENT_SCOPE)
    elseif(language_id STREQUAL "GNU")
        set(${out_var} ${JFX_CLANG_WARNINGS} PARENT_SCOPE)
    elseif(language_id STREQUAL "MSVC")
        set(${out_var} "${JFX_MSVC_WARNINGS}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()

function(set_project_warnings target_name)
    if(NOT TARGET ${target_name})
        message(FATAL_ERROR "set_project_warnings: no such target '${target_name}'")
    endif()

    _jfx_select_warnings("${CMAKE_C_COMPILER_ID}" C_WARNINGS)
    _jfx_select_warnings("${CMAKE_CXX_COMPILER_ID}" CXX_WARNINGS)

    if(CXX_WARNINGS STREQUAL C_WARNINGS)
        target_compile_options(${target_name} PRIVATE ${C_WARNINGS})
    else()
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:C>:${C_WARNINGS}>
            $<$<COMPILE_LANGUAGE:CXX>:${CXX_WARNINGS}>
        )
    endif()

    # C++-only extras, guarded by both language and compiler: MSVC rejects
    # GCC/Clang spellings outright.
    if(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
        target_compile_options(${target_name} PRIVATE
            $<$<COMPILE_LANGUAGE:CXX>:${JFX_CXX_CLANG_WARNINGS}>)
    endif()
endfunction()
