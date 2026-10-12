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
if(JFX_BACKEND_NOVOGFX)
    # The Rust half builds through cargo; without a toolchain the backend is
    # dropped with a warning rather than failing a configure that never
    # asked for it. The option only exists on Linux (see the root listfile).
    find_program(CARGO_EXECUTABLE cargo)
    if(NOT CARGO_EXECUTABLE)
        message(WARNING
            "JFX_BACKEND_NOVOGFX is ON but cargo was not found; disabling "
            "the NovoGFX backend. Install a Rust toolchain (1.82+) to "
            "enable it.")
        set(JFX_BACKEND_NOVOGFX OFF CACHE BOOL
            "Build the NovoGFX DRM/KMS backend (Linux only)" FORCE)
    else()
        list(APPEND JFX_ENABLED_BACKENDS novogfx)
        add_subdirectory(novogfx)
    endif()
endif()

if(NOT JFX_ENABLED_BACKENDS)
    message(FATAL_ERROR
        "No backend enabled. Turn on at least one of JFX_BACKEND_VULKAN, "
        "JFX_BACKEND_METAL, JFX_BACKEND_D3D12, JFX_BACKEND_WEBGPU, "
        "JFX_BACKEND_NOVOGFX.")
endif()

# core/ is a sibling directory and cannot see this scope's variable, so publish
# the list globally: the engine links and registers exactly these backends.
set_property(GLOBAL PROPERTY JFX_ENABLED_BACKENDS "${JFX_ENABLED_BACKENDS}")
