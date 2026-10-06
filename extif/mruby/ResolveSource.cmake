set(JFX_MRUBY_ROOT "${PROJECT_SOURCE_DIR}/third_party/mruby" CACHE PATH
    "Path to a compatible mruby source checkout (tested with 3.3.0)")
set(JFX_MRUBY_ALLOCATOR_API "")
if(EXISTS "${JFX_MRUBY_ROOT}/include/mruby.h")
    file(STRINGS "${JFX_MRUBY_ROOT}/include/mruby.h" JFX_MRUBY_ALLOCATOR_API
        REGEX "mrb_open_allocf" LIMIT_COUNT 1)
endif()

# Keep compatible vendored sources and explicit external roots. Newer mruby
# removed the per-state allocator, so use a pinned compatible release when the
# default checkout cannot enforce sandbox budgets. Do not overwrite the cache:
# the fetched source belongs to this build tree, not to the user's source tree.
if(NOT JFX_MRUBY_ALLOCATOR_API AND
        JFX_MRUBY_ROOT STREQUAL "${PROJECT_SOURCE_DIR}/third_party/mruby")
    message(STATUS "Vendored mruby missing or incompatible; using mruby 3.3.0 "
        "(set JFX_MRUBY_ROOT to a compatible checkout for offline builds)")
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(jfx_mruby_source
        URL https://codeload.github.com/mruby/mruby/tar.gz/refs/tags/3.3.0
        URL_HASH SHA256=53088367e3d7657eb722ddfacb938f74aed1f8538b3717fe0b6eb8f58402af65)
    FetchContent_MakeAvailable(jfx_mruby_source)
    set(JFX_MRUBY_ROOT "${jfx_mruby_source_SOURCE_DIR}")
endif()

if(NOT EXISTS "${JFX_MRUBY_ROOT}/include/mruby.h")
    message(FATAL_ERROR "mruby headers not found at '${JFX_MRUBY_ROOT}'. "
        "Set -DJFX_MRUBY_ROOT=<mruby-3.3.0-checkout>.")
endif()
file(STRINGS "${JFX_MRUBY_ROOT}/include/mruby.h" JFX_MRUBY_ALLOCATOR_API
    REGEX "mrb_open_allocf" LIMIT_COUNT 1)
if(NOT JFX_MRUBY_ALLOCATOR_API)
    message(FATAL_ERROR "This mruby checkout lacks mrb_open_allocf, required "
        "for JoltFX's sandbox memory budgets. Use mruby 3.3.0 via "
        "-DJFX_MRUBY_ROOT=<checkout>, or disable mruby with -DJFX_EXT_MRUBY=OFF.")
endif()
