cmake_minimum_required(VERSION 3.20)
project(novogfx-c VERSION 0.3.0 LANGUAGES C CXX)

option(NOVOGFX_BUILD_STATIC "Build against the novogfx staticlib" ON)

find_program(CARGO cargo REQUIRED)

# Cargo-built Rust library (cdylib + staticlib). The crate type in
# capi/novogfx-c/Cargo.toml emits libnovogfx_c.so / libnovogfx_c.a into
# the workspace target dir; we import it here so C/C++ consumers link it.
set(NOVOGFX_MANIFEST "${CMAKE_CURRENT_SOURCE_DIR}/Cargo.toml")
set(NOVOGFX_TARGET_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../target")

if(NOVOGFX_BUILD_STATIC)
  set(NOVOGFX_RUST_LIB "${NOVOGFX_TARGET_DIR}/release/libnovogfx_c.a")
else()
  set(NOVOGFX_RUST_LIB "${NOVOGFX_TARGET_DIR}/release/libnovogfx_c.so")
endif()

add_custom_target(novogfx_c_rust ALL
  COMMAND ${CARGO} build --manifest-path ${NOVOGFX_MANIFEST} --release --target-dir ${NOVOGFX_TARGET_DIR}
  BYPRODUCTS ${NOVOGFX_RUST_LIB}
  COMMENT "Building novogfx-c via Cargo (release)"
  VERBATIM)

add_library(novogfx_c UNKNOWN IMPORTED GLOBAL)
set_target_properties(novogfx_c PROPERTIES IMPORTED_LOCATION "${NOVOGFX_RUST_LIB}")
add_dependencies(novogfx_c novogfx_c_rust)
target_include_directories(novogfx_c INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}")

find_package(Threads REQUIRED)
target_link_libraries(novogfx_c INTERFACE Threads::Threads dl m drm)
target_include_directories(novogfx_c INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/../..")

install(FILES novogfx.h novogfx.hpp DESTINATION include)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../DSLtk.hpp" DESTINATION include)
install(FILES "${NOVOGFX_RUST_LIB}" DESTINATION lib)
