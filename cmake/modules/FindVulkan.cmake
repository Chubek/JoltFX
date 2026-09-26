# Placeholder for Vulkan finder
# Use find_package(Vulkan REQUIRED) in CMake 3.21+
find_package(Vulkan QUIET)

if(NOT Vulkan_FOUND)
    message(STATUS "Vulkan SDK not found, skipping Vulkan backend")
endif()
