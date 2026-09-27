# Cross-compilation toolchain for the Emscripten/WebAssembly target.
#
# Requires the Emscripten SDK on PATH (emcc, emcmake). Invoke through emcmake so
# the Emscripten-specific CMake platform modules are on the toolchain path:
#
#   emcmake cmake -S . -B build-wasm \
#       -DCMAKE_TOOLCHAIN_FILE=cmake/toolchains/emscripten.cmake \
#       -DJFX_BACKEND_VULKAN=OFF -DJFX_BACKEND_D3D12=OFF
#   cmake --build build-wasm
#
# Only the engine, the Joltscript layers and the WASM-facing surfaces build
# here. The desktop frontend is not portable to WASM yet (it needs SDL2 and
# OpenGL), the host-application bridges have no WASM meaning, and the extension
# runtimes are not built for WASM.

set(CMAKE_SYSTEM_NAME Emscripten)
set(CMAKE_SYSTEM_VERSION 1)

# Emscripten ships its own libc, so nothing from the host is used.
set(CMAKE_C_COMPILER emcc)
set(CMAKE_CXX_COMPILER em++)

# The engine and the execution layer are plain C11; the desktop frontend and the
# tests are the only C++ consumers and are not part of a WASM build.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# 64-bit is required: the engine and the bytecode validator both use size_t and
# uint64_t arithmetic that assumes a 64-bit address space.
set(CMAKE_SIZEOF_VOID_P 8)

# Match the browser ABI: no pthreads unless explicitly requested, exceptions off,
# and a bounded initial memory so a large .joltpkg cannot exhaust a 32-bit
# WebAssembly address space at instantiation time.
set(JFX_WASM_MEMORY_INITIAL "33554432") # 32 MiB
set(JFX_WASM_MEMORY_MAXIMUM "268435456") # 256 MiB
set(CMAKE_EXECUTABLE_SUFFIX ".js")

set(CMAKE_EXE_LINKER_FLAGS_INIT
    "-sEXPORTED_FUNCTIONS=['_malloc','_free'] \
-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','HEAPU8','HEAPF32'] \
-sALLOW_MEMORY_GROWTH=1 \
-sINITIAL_MEMORY=${JFX_WASM_MEMORY_INITIAL} \
-sMAXIMUM_MEMORY=${JFX_WASM_MEMORY_MAXIMUM} \
-sSTACK_SIZE=1048576 \
-sMODULARIZE=1 \
-sEXPORT_NAME=JoltFX \
-sENVIRONMENT=web,worker \
-DDISABLE_EXCEPTION_CATCHING=1"
)
