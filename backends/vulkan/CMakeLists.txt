find_package(Vulkan)

if(Vulkan_FOUND)
    add_library(jfx_backend_vulkan STATIC
        src/vulkan_backend.c
    )

    target_include_directories(jfx_backend_vulkan PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )

    target_link_libraries(jfx_backend_vulkan PUBLIC
        jfx_core
        Vulkan::Vulkan
    )

    set_project_warnings(jfx_backend_vulkan)
else()
    message(STATUS "Vulkan not found, skipping Vulkan backend")
endif()
