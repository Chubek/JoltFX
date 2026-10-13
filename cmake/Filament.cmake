option(JFX_FILAMENT "Build the optional Filament 3D renderer" OFF)
if(JFX_FILAMENT)
    if(CMAKE_CROSSCOMPILING OR NOT CMAKE_SYSTEM_NAME MATCHES "^(Linux|Darwin)$")
        message(FATAL_ERROR "JFX_FILAMENT currently supports native Linux/macOS builds")
    endif()
    # Filament requires C++20. Keep its compiler flags, targets, dependencies and
    # allocator/runtime implementation behind a standalone private C bridge.
    find_program(JFX_FILAMENT_CLANG clang REQUIRED)
    find_program(JFX_FILAMENT_CLANGXX clang++ REQUIRED)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    include(ExternalProject)
    include(GNUInstallDirs)
    set(JFX_FILAMENT_BUILD_DIR "${PROJECT_BINARY_DIR}/filament" CACHE PATH "Isolated Filament build directory")
    set(JFX_FILAMENT_BUILD_TYPE Release CACHE STRING "Filament SDK build configuration")
    set(JFX_FILAMENT_BUILD_JOBS 1 CACHE STRING "Parallel jobs for the isolated Filament build")
    set(jfx_filament_dir "${JFX_FILAMENT_BUILD_DIR}")
    set(jfx_filament_library "${jfx_filament_dir}/libjfx_filament${CMAKE_SHARED_LIBRARY_SUFFIX}")
    ExternalProject_Add(jfx_filament_build
        SOURCE_DIR "${PROJECT_SOURCE_DIR}/backends/filament"
        BINARY_DIR "${jfx_filament_dir}"
        CMAKE_ARGS
            "-DFILAMENT_ROOT=${PROJECT_SOURCE_DIR}/third_party/filament"
            "-DCMAKE_C_COMPILER=${JFX_FILAMENT_CLANG}"
            "-DCMAKE_CXX_COMPILER=${JFX_FILAMENT_CLANGXX}"
            "-DCMAKE_BUILD_TYPE=${JFX_FILAMENT_BUILD_TYPE}"
            "-DJFX_FILAMENT_ASAN=${JFX_ASAN}"
            "-DJFX_FILAMENT_UBSAN=${JFX_UBSAN}"
            "-DPython3_EXECUTABLE=${Python3_EXECUTABLE}"
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target jfx_filament_bridge --parallel ${JFX_FILAMENT_BUILD_JOBS}
        INSTALL_COMMAND ""
        BUILD_ALWAYS TRUE
        BUILD_BYPRODUCTS "${jfx_filament_library}")
    add_library(jfx_filament_native SHARED IMPORTED GLOBAL)
    set_target_properties(jfx_filament_native PROPERTIES IMPORTED_LOCATION "${jfx_filament_library}")
    add_dependencies(jfx_filament_native jfx_filament_build)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(jfx_filament_native PROPERTIES IMPORTED_NO_SONAME TRUE)
        list(APPEND CMAKE_INSTALL_RPATH "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
    else()
        list(APPEND CMAKE_INSTALL_RPATH "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
    endif()
    install(FILES "${jfx_filament_library}" DESTINATION "${CMAKE_INSTALL_LIBDIR}")
endif()
