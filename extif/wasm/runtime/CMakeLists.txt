# Keep upstream directory-wide definitions and include paths in this scope.
set(WAMR_ROOT_DIR "${JFX_WAMR_ROOT}")
if(ANDROID)
    set(WAMR_BUILD_PLATFORM android)
elseif(APPLE)
    set(WAMR_BUILD_PLATFORM darwin)
elseif(WIN32)
    set(WAMR_BUILD_PLATFORM windows)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(WAMR_BUILD_PLATFORM linux)
else()
    message(FATAL_ERROR "JFX_EXT_WASM: unsupported WAMR host platform ${CMAKE_SYSTEM_NAME}")
endif()
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_INSTRUCTION_METERING 1)
set(WAMR_BUILD_ALLOC_WITH_USAGE 1)
if(JFX_UBSAN)
    # Use upstream's UBSan profile: its four-byte VM stack deliberately stores
    # native pointer records with four-byte alignment, so alignment is excluded
    # in WAMR only. The adapter keeps the full project sanitizer set.
    set(WAMR_BUILD_SANITIZER ubsan)
endif()
set(WAMR_DISABLE_HW_BOUND_CHECK 1)
foreach(feature AOT JIT FAST_JIT LIBC_BUILTIN LIBC_WASI LIBC_UVWASI LIBC_EMCC
        MULTI_MODULE LIB_PTHREAD LIB_WASI_THREADS THREAD_MGR SHARED_MEMORY
        SIMD REF_TYPES GC STRINGREF DEBUG_INTERP WASM_CACHE)
    set(WAMR_BUILD_${feature} 0)
endforeach()
include("${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake")
find_package(Threads REQUIRED)
add_library(jfx_wamr_runtime STATIC ${WAMR_RUNTIME_LIB_SOURCE})
if(JFX_UBSAN)
    # Directory sanitizer options follow CMAKE_C_FLAGS; repeat the upstream
    # exception last so the inherited -fsanitize=undefined cannot reenable it.
    target_compile_options(jfx_wamr_runtime PRIVATE -fno-sanitize=alignment)
endif()
set_target_properties(jfx_wamr_runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(jfx_wamr_runtime SYSTEM INTERFACE
    $<BUILD_INTERFACE:${WAMR_ROOT_DIR}/core/iwasm/include>)
target_link_libraries(jfx_wamr_runtime PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX)
    target_link_libraries(jfx_wamr_runtime PUBLIC m)
elseif(WIN32)
    target_link_libraries(jfx_wamr_runtime PUBLIC ntdll ws2_32)
endif()
