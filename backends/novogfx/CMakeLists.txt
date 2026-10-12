# NovoGFX backend: Rust DRM/KMS library (novogfx-c) plus the JoltFX HAL
# adapter. Linux-only; the root CMake gates the option by platform.
#
# The Rust workspace is dependency-free, so the cargo build is fully offline
# and reproducible; --locked pins the checked-in Cargo.lock. Artifacts go to
# the build tree, not the workspace's target/ directory, so a JoltFX build
# never dirties the Rust checkout.

# The parent scope guarantees cargo exists before adding this directory; a
# standalone configure of this file fails fast instead of emitting a broken
# imported-library rule.
find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(NOVOGFX_CAPI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/capi/novogfx-c")
set(NOVOGFX_CARGO_TARGET_DIR "${CMAKE_CURRENT_BINARY_DIR}/cargo-target")

# Single-config generators honour CMAKE_BUILD_TYPE; multi-config generators
# get the release profile (cargo profiles are per-invocation, not per-config).
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(NOVOGFX_CARGO_PROFILE "debug")
    set(NOVOGFX_CARGO_PROFILE_ARGS "")
else()
    set(NOVOGFX_CARGO_PROFILE "release")
    set(NOVOGFX_CARGO_PROFILE_ARGS "--release")
endif()

set(NOVOGFX_RUST_LIB
    "${NOVOGFX_CARGO_TARGET_DIR}/${NOVOGFX_CARGO_PROFILE}/libnovogfx_c.a")

# Rebuild when any Rust source or manifest changes. GLOB_RECURSE with
# CONFIGURE_DEPENDS re-globs at build time, so new files are picked up
# without a manual re-configure.
file(GLOB_RECURSE NOVOGFX_RUST_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/crates/*.rs"
    "${CMAKE_CURRENT_SOURCE_DIR}/crates/*/Cargo.toml"
    "${NOVOGFX_CAPI_DIR}/src/*.rs"
    "${NOVOGFX_CAPI_DIR}/Cargo.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.lock")

add_custom_command(
    OUTPUT "${NOVOGFX_RUST_LIB}"
    COMMAND ${CARGO_EXECUTABLE} build --locked --offline
        --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml"
        --package novogfx-c
        --target-dir "${NOVOGFX_CARGO_TARGET_DIR}"
        ${NOVOGFX_CARGO_PROFILE_ARGS}
    DEPENDS ${NOVOGFX_RUST_SOURCES}
    COMMENT "Building novogfx-c (${NOVOGFX_CARGO_PROFILE}) via cargo"
    VERBATIM)

add_custom_target(novogfx_c_rust DEPENDS "${NOVOGFX_RUST_LIB}")

add_library(novogfx_c STATIC IMPORTED GLOBAL)
set_target_properties(novogfx_c PROPERTIES
    IMPORTED_LOCATION "${NOVOGFX_RUST_LIB}")
add_dependencies(novogfx_c novogfx_c_rust)
target_include_directories(novogfx_c INTERFACE "${NOVOGFX_CAPI_DIR}")

# The Rust staticlib carries its own Rust std objects; its system needs are
# pthread/dl/m plus libdrm, which novogfx-sys links for the ioctls.
find_package(Threads REQUIRED)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(JFX_LIBDRM QUIET libdrm)
endif()
if(JFX_LIBDRM_FOUND)
    target_link_libraries(novogfx_c INTERFACE ${JFX_LIBDRM_LINK_LIBRARIES})
    target_link_directories(novogfx_c INTERFACE ${JFX_LIBDRM_LIBRARY_DIRS})
else()
    target_link_libraries(novogfx_c INTERFACE drm)
endif()
target_link_libraries(novogfx_c INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)

add_library(jfx_backend_novogfx STATIC jfx/src/novogfx_backend.c)

target_include_directories(jfx_backend_novogfx PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/jfx/include>
    $<INSTALL_INTERFACE:include>
)

# PUBLIC jfx_backend_software: the adapter's CPU execution path, matching the
# metal/d3d12/webgpu adapters. novogfx_c is PRIVATE: Rust FFI types never
# cross the backend's public surface.
target_link_libraries(jfx_backend_novogfx PUBLIC
    jfx_backend_software
    jfx_api
)
target_link_libraries(jfx_backend_novogfx PRIVATE novogfx_c)

set_project_warnings(jfx_backend_novogfx)

include(GNUInstallDirs)
install(FILES "${NOVOGFX_RUST_LIB}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES "${NOVOGFX_CAPI_DIR}/novogfx.h" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
