# Isolate upstream options, generated headers and install rules from JoltFX.
include(ExternalProject)
set(JFX_JOLTTYPE_VENDOR_PREFIX "${CMAKE_CURRENT_BINARY_DIR}/prefix" CACHE PATH "JoltType vendored dependency installation")
set(JFX_JOLTTYPE_PREBUILT_VENDOR_PREFIX "" CACHE PATH "Reuse an existing JoltType vendored dependency build (e.g. for sanitizer runs)")
if(JFX_JOLTTYPE_PREBUILT_VENDOR_PREFIX)
    set(JFX_JOLTTYPE_VENDOR_PREFIX "${JFX_JOLTTYPE_PREBUILT_VENDOR_PREFIX}")
endif()
file(MAKE_DIRECTORY "${JFX_JOLTTYPE_VENDOR_PREFIX}/include")
set(common_args -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${JFX_JOLTTYPE_VENDOR_PREFIX}
    -DCMAKE_INSTALL_LIBDIR=lib -DCMAKE_POSITION_INDEPENDENT_CODE=ON
    -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
function(jt_vendor name source library)
    set(runtime_file "lib/${CMAKE_SHARED_LIBRARY_PREFIX}${library}${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(WIN32)
        if(name STREQUAL "qpdf")
            set(runtime_file "bin/qpdf30.dll")
        else()
            set(runtime_file "bin/${library}.dll")
        endif()
    endif()
    if(NOT JFX_JOLTTYPE_PREBUILT_VENDOR_PREFIX)
    ExternalProject_Add(jt_${name}_build
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/third_party/${source}"
        BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/${name}"
        CMAKE_ARGS ${common_args} ${ARGN}
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --parallel 4
        BUILD_BYPRODUCTS "${JFX_JOLTTYPE_VENDOR_PREFIX}/${runtime_file}"
        UPDATE_COMMAND "")
    elseif(NOT EXISTS "${JFX_JOLTTYPE_VENDOR_PREFIX}/${runtime_file}")
        message(FATAL_ERROR "Missing ${library} in JFX_JOLTTYPE_PREBUILT_VENDOR_PREFIX")
    endif()
    add_library(jt_${name} SHARED IMPORTED GLOBAL)
    set_target_properties(jt_${name} PROPERTIES
        IMPORTED_LOCATION "${JFX_JOLTTYPE_VENDOR_PREFIX}/${runtime_file}"
        INTERFACE_INCLUDE_DIRECTORIES "${JFX_JOLTTYPE_VENDOR_PREFIX}/include")
    if(WIN32)
        set_target_properties(jt_${name} PROPERTIES IMPORTED_IMPLIB "${JFX_JOLTTYPE_VENDOR_PREFIX}/lib/${library}.lib")
    endif()
    if(NOT JFX_JOLTTYPE_PREBUILT_VENDOR_PREFIX)
        add_dependencies(jt_${name} jt_${name}_build)
    endif()
endfunction()
if(JFX_JOLTTYPE_PDF)
    jt_vendor(hpdf libharu hpdf -DBUILD_SHARED_LIBS=ON -DLIBHPDF_EXAMPLES=OFF
        -DCMAKE_REQUIRE_FIND_PACKAGE_PNG=ON -DCMAKE_REQUIRE_FIND_PACKAGE_ZLIB=ON)
    jt_vendor(podofo podofo podofo -DPODOFO_BUILD_LIB_ONLY=ON -DPODOFO_BUILD_STATIC=OFF
        -DPODOFO_WITH_AFDKO=OFF -DPODOFO_WITH_LTO=OFF)
    jt_vendor(qpdf qpdf qpdf -DBUILD_STATIC_LIBS=OFF -DBUILD_SHARED_LIBS=ON
        -DBUILD_DOC=OFF -DINSTALL_EXAMPLES=OFF -DINSTALL_MANUAL=OFF)
endif()
if(JFX_JOLTTYPE_MODULES)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(binaryen_link_flags "-DCMAKE_SHARED_LINKER_FLAGS=-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/binaryen.exports")
    endif()
    jt_vendor(binaryen binaryen binaryen -DBUILD_TESTS=OFF -DBUILD_TOOLS=OFF
        -DBUILD_LLVM_DWARF=ON -DBUILD_SHARED_LIBS=ON -DENABLE_WERROR=OFF ${binaryen_link_flags})
endif()
install(DIRECTORY "${JFX_JOLTTYPE_VENDOR_PREFIX}/lib/" DESTINATION lib
    OPTIONAL
    FILES_MATCHING PATTERN "*.so*" PATTERN "*.dylib" PATTERN "*.dll" PATTERN "*.lib"
    PATTERN "cmake" EXCLUDE PATTERN "pkgconfig" EXCLUDE)
if(WIN32)
    install(DIRECTORY "${JFX_JOLTTYPE_VENDOR_PREFIX}/bin/" DESTINATION bin OPTIONAL FILES_MATCHING PATTERN "*.dll")
endif()
