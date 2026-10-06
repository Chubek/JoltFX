# Only the MIT-licensed ABI interfaces are needed; no SDK GUI or audio driver.
if(EMSCRIPTEN OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
    set(JFX_VST3_DEFAULT OFF)
else()
    set(JFX_VST3_DEFAULT ON)
endif()
option(JFX_AUDIO_VST3 "Host native VST3 audio effects" ${JFX_VST3_DEFAULT})
if(JFX_AUDIO_VST3)
    if(EMSCRIPTEN OR ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "iOS")
        message(FATAL_ERROR "VST3 hosting requires a native desktop platform")
    endif()
    include(FetchContent)
    if(POLICY CMP0135)
        cmake_policy(SET CMP0135 NEW)
    endif()
    FetchContent_Declare(jfx_vst3_interfaces
        URL https://codeload.github.com/steinbergmedia/vst3_pluginterfaces/tar.gz/4f547e8e102b47de4a8b8aaf343c73b700786372
        URL_HASH SHA256=ee70777eebb8450fc04fae2a6dd452f723d12bfb8222af443b8c1f8a9259089a
        SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/vst3/pluginterfaces")
    FetchContent_MakeAvailable(jfx_vst3_interfaces)
    get_filename_component(JFX_VST3_INCLUDE_DIR "${jfx_vst3_interfaces_SOURCE_DIR}" DIRECTORY)
    set(JFX_VST3_INCLUDE_DIR "${JFX_VST3_INCLUDE_DIR}" CACHE INTERNAL "VST3 interface parent")
    target_include_directories(jfx_core SYSTEM PRIVATE "${JFX_VST3_INCLUDE_DIR}")
    target_sources(jfx_core PRIVATE
        "${jfx_vst3_interfaces_SOURCE_DIR}/base/funknown.cpp"
        "${jfx_vst3_interfaces_SOURCE_DIR}/base/coreiids.cpp")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(
            "${jfx_vst3_interfaces_SOURCE_DIR}/base/funknown.cpp"
            "${jfx_vst3_interfaces_SOURCE_DIR}/base/coreiids.cpp"
            PROPERTIES COMPILE_OPTIONS "-Wno-shadow;-Wno-conversion;-Wno-sign-conversion;-Wno-format")
    endif()
    target_compile_definitions(jfx_core PUBLIC JFX_AUDIO_VST3=1)
    if(APPLE)
        target_link_libraries(jfx_core PRIVATE "-framework CoreFoundation")
    endif()
endif()
