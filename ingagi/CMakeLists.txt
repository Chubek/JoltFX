cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(Ingagi VERSION 1.0.0 LANGUAGES C CXX)
    include(CTest)
endif()
include(GNUInstallDirs)

option(INGAGI_BUILD_TESTS "Build Ingagi ABI and language-binding tests" ON)
option(INGAGI_NOVOPARSE "Require native NovoParse recognition" OFF)
set(INGAGI_GOROOT "/usr/lib/go" CACHE PATH "GOROOT used by all Ingagi Go commands")
set(INGAGI_NOVOPARSE_LIBRARY_DIR "" CACHE PATH "Directory containing libnovoparse for tagged builds")
find_program(INGAGI_GO go HINTS "${INGAGI_GOROOT}/bin" REQUIRED)
if(NOT EXISTS "${INGAGI_GOROOT}/src/runtime")
    message(FATAL_ERROR "Set INGAGI_GOROOT to a Go 1.21+ distribution")
endif()
if(CMAKE_CROSSCOMPILING)
    message(FATAL_ERROR "Ingagi CMake currently builds host tools/libraries only; cross-compile Go explicitly")
endif()
if(NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
    message(FATAL_ERROR "Ingagi's CMake embedding build currently supports Linux/macOS hosts")
endif()

set(ingagi_env
    "GOROOT=${INGAGI_GOROOT}" "GOTOOLCHAIN=local" "GOPROXY=off"
    "GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/gocache" "CGO_ENABLED=1"
    "CC=${CMAKE_C_COMPILER}" "CXX=${CMAKE_CXX_COMPILER}")
set(ingagi_tags)
if(INGAGI_NOVOPARSE)
    if(NOT INGAGI_NOVOPARSE_LIBRARY_DIR)
        message(FATAL_ERROR "INGAGI_NOVOPARSE requires INGAGI_NOVOPARSE_LIBRARY_DIR; see scripts/test-native.sh")
    endif()
    list(APPEND ingagi_tags -tags novoparse)
    list(APPEND ingagi_env "CGO_LDFLAGS=-L${INGAGI_NOVOPARSE_LIBRARY_DIR} -Wl,-rpath,${INGAGI_NOVOPARSE_LIBRARY_DIR}")
endif()
execute_process(COMMAND ${CMAKE_COMMAND} -E env ${ingagi_env} ${INGAGI_GO} version
    RESULT_VARIABLE ingagi_go_status OUTPUT_VARIABLE ingagi_go_version)
if(NOT ingagi_go_status EQUAL 0 OR NOT ingagi_go_version MATCHES "go([0-9]+\\.[0-9]+)")
    message(FATAL_ERROR "Cannot run Go with INGAGI_GOROOT=${INGAGI_GOROOT}")
endif()
if(CMAKE_MATCH_1 VERSION_LESS "1.21")
    message(FATAL_ERROR "Ingagi requires Go 1.21 or newer")
endif()

# Go's compiler tracks imports internally; CMake tracks source/header/grammar
# changes to re-invoke it. Vendor math headers are included for incremental builds.
file(GLOB_RECURSE ingagi_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/*.go" "${CMAKE_CURRENT_SOURCE_DIR}/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cc" "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.sexp")
file(GLOB_RECURSE ingagi_math_headers CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm/*.hpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/glm/*.inl"
    "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/simdette/*.hpp")
set(ingagi_lib_dir "${CMAKE_CURRENT_BINARY_DIR}/lib")
set(ingagi_bin_dir "${CMAKE_CURRENT_BINARY_DIR}/bin")
set(ingagi_library "${ingagi_lib_dir}/${CMAKE_SHARED_LIBRARY_PREFIX}ingagi_embed${CMAKE_SHARED_LIBRARY_SUFFIX}")
set(ingagi_cli_path "${ingagi_bin_dir}/ingagi${CMAKE_EXECUTABLE_SUFFIX}")
add_custom_command(OUTPUT "${ingagi_library}" "${ingagi_cli_path}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${ingagi_lib_dir}" "${ingagi_bin_dir}"
    COMMAND ${CMAKE_COMMAND} -E env ${ingagi_env} ${INGAGI_GO} build ${ingagi_tags}
        -buildmode=c-shared -o "${ingagi_library}" ./joltfx_embed
    COMMAND ${CMAKE_COMMAND} -E env ${ingagi_env} ${INGAGI_GO} build ${ingagi_tags}
        -o "${ingagi_cli_path}" ./cmd
    DEPENDS ${ingagi_sources} ${ingagi_math_headers} "${CMAKE_CURRENT_SOURCE_DIR}/go.mod"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "Building Ingagi CLI and C embedding ABI" VERBATIM)
add_custom_target(ingagi_build ALL DEPENDS "${ingagi_library}" "${ingagi_cli_path}")
add_library(ingagi_embed SHARED IMPORTED GLOBAL)
set_target_properties(ingagi_embed PROPERTIES IMPORTED_LOCATION "${ingagi_library}"
    INTERFACE_INCLUDE_DIRECTORIES "${CMAKE_CURRENT_SOURCE_DIR}/native_extension_api")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Go c-shared output has no SONAME. Link by library name so DT_NEEDED
    # does not capture a relative path that bypasses the executable's RUNPATH.
    set_target_properties(ingagi_embed PROPERTIES IMPORTED_NO_SONAME TRUE)
endif()
add_dependencies(ingagi_embed ingagi_build)

install(PROGRAMS "${ingagi_cli_path}" DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES "${ingagi_library}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
install(FILES native_extension_api/Ingagi-Module.h native_extension_api/Ingagi-Module.hpp
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/ingagi")
install(FILES native_extension_api/Ingagi-Module.py DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/bindings" RENAME ingagi_module.py)
install(FILES native_extension_api/Ingagi-Module.di DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/bindings" RENAME ingagi_module.di)
install(FILES native_extension_api/Ingagi-Module.rs DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/bindings" RENAME ingagi_module.rs)
install(FILES native_extension_api/Ingagi-Module.java DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/bindings" RENAME IngagiModule.java)
install(DIRECTORY compiler/targets/ DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/targets" FILES_MATCHING PATTERN "*.sexp")
install(FILES README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/docs")
install(FILES native_extension_api/README.md DESTINATION "${CMAKE_INSTALL_DATADIR}/ingagi/docs" RENAME native-extension-api.md)

if(INGAGI_BUILD_TESTS)
    enable_testing()
    add_executable(ingagi_abi_smoke joltfx_embed/tests/smoke.c)
    target_compile_features(ingagi_abi_smoke PRIVATE c_std_11)
    target_link_libraries(ingagi_abi_smoke PRIVATE ingagi_embed)
    add_executable(ingagi_cpp_smoke joltfx_embed/tests/smoke.cpp)
    target_compile_features(ingagi_cpp_smoke PRIVATE cxx_std_17)
    target_link_libraries(ingagi_cpp_smoke PRIVATE ingagi_embed)
    # Test assertions must execute their ABI calls in Release builds too.
    if(MSVC)
        target_compile_options(ingagi_abi_smoke PRIVATE /UNDEBUG)
        target_compile_options(ingagi_cpp_smoke PRIVATE /UNDEBUG)
    else()
        target_compile_options(ingagi_abi_smoke PRIVATE -UNDEBUG -Wall -Wextra -Werror)
        target_compile_options(ingagi_cpp_smoke PRIVATE -UNDEBUG -Wall -Wextra -Werror)
    endif()
    add_test(NAME ingagi_c_abi COMMAND ingagi_abi_smoke)
    add_test(NAME ingagi_cpp_binding COMMAND ingagi_cpp_smoke)
    add_test(NAME ingagi_go COMMAND ${CMAKE_COMMAND} -E env ${ingagi_env} ${INGAGI_GO} test ${ingagi_tags} ./... -timeout 120s)
    set_tests_properties(ingagi_go PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 180)
    find_package(Python3 QUIET COMPONENTS Interpreter)
    if(Python3_FOUND)
        add_test(NAME ingagi_python_binding COMMAND ${CMAKE_COMMAND} -E env PYTHONDONTWRITEBYTECODE=1
            ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/joltfx_embed/tests/smoke.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/native_extension_api/Ingagi-Module.py" "${ingagi_library}")
    endif()
    if(UNIX)
        find_program(INGAGI_RUSTC rustc)
        if(INGAGI_RUSTC)
            add_custom_command(OUTPUT "${ingagi_bin_dir}/rust_smoke"
                COMMAND ${INGAGI_RUSTC} --edition=2021 "${CMAKE_CURRENT_SOURCE_DIR}/joltfx_embed/tests/smoke.rs"
                    -L "${ingagi_lib_dir}" -C "link-arg=-Wl,-rpath,${ingagi_lib_dir}" -o "${ingagi_bin_dir}/rust_smoke"
                DEPENDS ingagi_build native_extension_api/Ingagi-Module.rs joltfx_embed/tests/smoke.rs VERBATIM)
            add_custom_target(ingagi_rust_test ALL DEPENDS "${ingagi_bin_dir}/rust_smoke")
            add_test(NAME ingagi_rust_binding COMMAND "${ingagi_bin_dir}/rust_smoke")
        endif()
        find_program(INGAGI_LDC ldc2)
        if(INGAGI_LDC)
            file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/bindings")
            configure_file(native_extension_api/Ingagi-Module.di "${CMAKE_CURRENT_BINARY_DIR}/bindings/ingagi_module.di" COPYONLY)
            add_custom_command(OUTPUT "${ingagi_bin_dir}/d_smoke"
                COMMAND ${INGAGI_LDC} "${CMAKE_CURRENT_SOURCE_DIR}/joltfx_embed/tests/smoke.d"
                    "-I${CMAKE_CURRENT_BINARY_DIR}/bindings" "-L-L${ingagi_lib_dir}"
                    "-L-rpath=${ingagi_lib_dir}" -L-lingagi_embed "-of=${ingagi_bin_dir}/d_smoke"
                DEPENDS ingagi_build native_extension_api/Ingagi-Module.di joltfx_embed/tests/smoke.d VERBATIM)
            add_custom_target(ingagi_d_test ALL DEPENDS "${ingagi_bin_dir}/d_smoke")
            add_test(NAME ingagi_d_binding COMMAND "${ingagi_bin_dir}/d_smoke")
        endif()
    endif()
    find_package(Java 22 QUIET COMPONENTS Runtime Development)
    if(Java_FOUND)
        file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/classes")
        add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/classes/Smoke.class"
            COMMAND ${Java_JAVAC_EXECUTABLE} -d "${CMAKE_CURRENT_BINARY_DIR}/classes"
                "${CMAKE_CURRENT_SOURCE_DIR}/native_extension_api/Ingagi-Module.java"
                "${CMAKE_CURRENT_SOURCE_DIR}/joltfx_embed/tests/Smoke.java"
            DEPENDS native_extension_api/Ingagi-Module.java joltfx_embed/tests/Smoke.java VERBATIM)
        add_custom_target(ingagi_java_test ALL DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/classes/Smoke.class")
        add_test(NAME ingagi_java_binding COMMAND ${Java_JAVA_EXECUTABLE} --enable-native-access=ALL-UNNAMED
            -cp "${CMAKE_CURRENT_BINARY_DIR}/classes" Smoke "${ingagi_library}")
    endif()
endif()
