# OpenFX host adapter. Depends only on the C API surface of the engine: the
# property, parameter and image-effect suites are implemented here, so this
# target needs no OpenFX host SDK and links no proprietary host library.
add_library(jfx_ofx_host STATIC
    src/ofx_property.c
    src/ofx_suites.c
    src/ofx_host.c
)
target_include_directories(jfx_ofx_host PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
# The OFX headers are third-party API definitions this adapter implements
# against, so they are private: they must not leak into a consumer's includes.
target_include_directories(jfx_ofx_host PRIVATE ${OPENFX_INCLUDE_DIR})
target_link_libraries(jfx_ofx_host PUBLIC jfx_core)
set_project_warnings(jfx_ofx_host)

# Bundle discovery uses the platform's shared-library loader.
if(UNIX AND NOT APPLE)
    target_link_libraries(jfx_ofx_host PRIVATE ${CMAKE_DL_LIBS})
endif()
if(WIN32)
    target_link_libraries(jfx_ofx_host PRIVATE advapi32)
endif()

# A test OFX plugin, built as a real shared object so the loader, action
# lifecycle and suites are exercised exactly as they are for a third-party
# bundle. This is the only way to test the dynamic-loading path honestly.
set(OFX_TEST_PLUGIN_DIR "${PROJECT_SOURCE_DIR}/frontends/plugins/tests")
add_library(jfx_ofx_test_plugin MODULE "${OFX_TEST_PLUGIN_DIR}/gain_plugin.c")
target_include_directories(jfx_ofx_test_plugin PRIVATE
    ${OFX_TEST_PLUGIN_DIR}
    ${OPENFX_INCLUDE_DIR}
)
set_target_properties(jfx_ofx_test_plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "jfx_ofx_gain"
)
if(UNIX AND NOT APPLE)
    # A plugin resolves its host symbols at load time, so undefined symbols are
    # expected here exactly as they are for a third-party OFX binary.
    target_link_options(jfx_ofx_test_plugin PRIVATE -Wl,--allow-shlib-undefined)
endif()
set_project_warnings(jfx_ofx_test_plugin)