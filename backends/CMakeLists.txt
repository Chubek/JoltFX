# The shared CPU path is required by the Vulkan backend's fallback and by the
# metal/d3d12/webgpu adapters, so it always builds and is added first: the
# per-backend targets link against it.
add_subdirectory(common)

# Each backend is gated by its own option and contributes a compile definition
# so the engine's backend registry only offers what was actually built. Gating
# the link alone would leave the registry advertising a backend whose symbols
# are missing, which fails at link time with an unhelpful error.

set(JFX_ENABLED_BACKENDS "")

if(JFX_BACKEND_VULKAN)
    list(APPEND JFX_ENABLED_BACKENDS vulkan)
    add_subdirectory(vulkan)
endif()
if(JFX_BACKEND_METAL)
    list(APPEND JFX_ENABLED_BACKENDS metal)
    add_subdirectory(metal)
endif()
if(JFX_BACKEND_D3D12)
    list(APPEND JFX_ENABLED_BACKENDS d3d12)
    add_subdirectory(d3d12)
endif()
if(JFX_BACKEND_WEBGPU)
    list(APPEND JFX_ENABLED_BACKENDS webgpu)
    add_subdirectory(webgpu)
endif()

if(NOT JFX_ENABLED_BACKENDS)
    message(FATAL_ERROR
        "No backend enabled. Turn on at least one of JFX_BACKEND_VULKAN, "
        "JFX_BACKEND_METAL, JFX_BACKEND_D3D12, JFX_BACKEND_WEBGPU.")
endif()

# core/ is a sibling directory and cannot see this scope's variable, so publish
# the list globally: the engine links and registers exactly these backends.
set_property(GLOBAL PROPERTY JFX_ENABLED_BACKENDS "${JFX_ENABLED_BACKENDS}")
