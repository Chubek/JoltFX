# The Phase 2 backend always builds: without a driver, pixels execute
# through the validated CPU fallback wired to the Joltscript execution
# layer. With a driver, JBC1 programs are code-generated to GLSL compute
# shaders, compiled with glslc (cached by content hash), and dispatched to
# the device. The build never requires the Vulkan SDK: the ABI subset in
# vk_minimal.h is resolved at runtime with dlopen. When the SDK is found,
# its headers are available for the future native dispatch path.
add_library(jfx_backend_vulkan STATIC
    src/vulkan_backend.c
    src/vk_loader.c
    src/vk_compute.c
)

target_include_directories(jfx_backend_vulkan PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(jfx_backend_vulkan PUBLIC
    joltscript_glue
    jfx_backend_software
    jfx_api
)

target_link_libraries(jfx_backend_vulkan PRIVATE
    tilly
    ${CMAKE_DL_LIBS}
)

if(NOT MSVC)
    target_link_libraries(jfx_backend_vulkan PUBLIC m)
endif()

find_package(Vulkan)
if(Vulkan_FOUND)
    target_link_libraries(jfx_backend_vulkan PUBLIC Vulkan::Vulkan)
    target_compile_definitions(jfx_backend_vulkan PUBLIC JFX_VK_SDK_AVAILABLE=1)
else()
    message(STATUS "Vulkan SDK not found, building Vulkan backend without SDK headers")
endif()

# vk_compute.c bounds its SPIR-V cache paths with an explicit length check and
# refuses to build a path that would not fit (see cache_paths), but GCC reasons
# about the fixed 4096-byte array rather than that check and still reports
# -Wformat-truncation on the two snprintf calls it can see. The check is real;
# the warning is not actionable here, so it is off for this file only.
set_source_files_properties(src/vk_compute.c PROPERTIES
    COMPILE_OPTIONS "-Wno-format-truncation")

set_project_warnings(jfx_backend_vulkan)
