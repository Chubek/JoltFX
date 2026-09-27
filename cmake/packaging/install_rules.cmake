include(GNUInstallDirs)

# The enabled-backend list lives in backends/' scope; read it from the global
# property the backends directory published.
get_property(JFX_ENABLED_BACKENDS GLOBAL PROPERTY JFX_ENABLED_BACKENDS)

# Ship the engine, not just the frontends. The previous rules installed the
# frontend and bridge libraries but omitted tilly, the Joltscript layers and the
# backends that jfx_core dispatches through, so a package could not actually run
# a kernel. Only targets that exist in this configuration are installed, which
# keeps a reduced build (options off) installable too.
set(JFX_INSTALL_TARGETS "")

# Always present: the runtime foundation and the engine.
list(APPEND JFX_INSTALL_TARGETS
    tillyz
    tilly
    joltscript_execution
    joltscript_glue
    jfx_core
    jfx_backend_software
    jolt_effects
    jfx_api
)

foreach(backend IN LISTS JFX_ENABLED_BACKENDS)
    list(APPEND JFX_INSTALL_TARGETS jfx_backend_${backend})
endforeach()
if(JFX_EXT_LUA OR JFX_EXT_MRUBY)
    list(APPEND JFX_INSTALL_TARGETS jfx_extif_common)
endif()
if(JFX_EXT_LUA)
    list(APPEND JFX_INSTALL_TARGETS jfx_lua)
endif()
if(JFX_EXT_MRUBY)
    list(APPEND JFX_INSTALL_TARGETS jfx_mruby)
endif()
if(JFX_FRONTEND_CLI)
    list(APPEND JFX_INSTALL_TARGETS jfx_frontend_common joltfx_cli)
endif()
if(JFX_FRONTEND_DESKTOP)
    list(APPEND JFX_INSTALL_TARGETS jfx_desktop_frontend jfx_desktop)
endif()
if(JFX_FRONTEND_WEB)
    list(APPEND JFX_INSTALL_TARGETS jfx_web_session)
endif()
if(JFX_FRONTEND_MOBILE)
    list(APPEND JFX_INSTALL_TARGETS jfx_mobile_player)
endif()
if(JFX_PLUGIN_HOST_BRIDGES)
    list(APPEND JFX_INSTALL_TARGETS
        jfx_host_plugin_common
        jfx_ae_plugin
        jfx_premiere_plugin
        jfx_davinci_plugin
    )
endif()

# Drop anything this configuration did not build, so the install rules never
# reference a target that does not exist.
set(JFX_INSTALL_TARGETS_PRESENT "")
foreach(target IN LISTS JFX_INSTALL_TARGETS)
    if(TARGET ${target})
        list(APPEND JFX_INSTALL_TARGETS_PRESENT ${target})
    endif()
endforeach()

install(TARGETS ${JFX_INSTALL_TARGETS_PRESENT}
    EXPORT JoltFXTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY core/include/jfx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY backends/common/include/jfx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING
    PATTERN "software_backend.h"
)
foreach(backend IN LISTS JFX_ENABLED_BACKENDS)
    install(DIRECTORY backends/${backend}/include/jfx
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING
        PATTERN "*_backend.h"
    )
endforeach()
install(DIRECTORY joltscript/layers/execution/include/joltscript
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY joltscript/layers/glue/include/joltscript
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)
install(DIRECTORY kernels/include/joltscript
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

foreach(frontend_dir IN ITEMS web mobile plugins desktop)
    if(EXISTS "${CMAKE_SOURCE_DIR}/frontends/${frontend_dir}/include/jfx")
        install(DIRECTORY "frontends/${frontend_dir}/include/jfx"
            DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        )
    endif()
endforeach()

set(JFX_INSTALL_DOCS README.md CHANGELOG.md PROGRESS.md)
foreach(doc IN LISTS JFX_INSTALL_DOCS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/${doc}")
        list(APPEND JFX_INSTALL_DOCS_PRESENT "${CMAKE_SOURCE_DIR}/${doc}")
    endif()
endforeach()
foreach(frontend_readme IN ITEMS cli desktop web mobile plugins)
    if(EXISTS "${CMAKE_SOURCE_DIR}/frontends/${frontend_readme}/README.md")
        list(APPEND JFX_INSTALL_DOCS_PRESENT
            "${CMAKE_SOURCE_DIR}/frontends/${frontend_readme}/README.md")
    endif()
endforeach()
if(JFX_INSTALL_DOCS_PRESENT)
    install(FILES ${JFX_INSTALL_DOCS_PRESENT}
        DESTINATION ${CMAKE_INSTALL_DOCDIR})
endif()

install(EXPORT JoltFXTargets
    FILE JoltFXTargets.cmake
    NAMESPACE JoltFX::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/JoltFX
)

set(CPACK_PACKAGE_NAME "JoltFX")
set(CPACK_PACKAGE_VENDOR "JoltFX")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Extensible motion-graphics suite: Joltscript kernels, a Core engine with pluggable backends, and CLI, desktop, web and mobile frontends.")
set(CPACK_PACKAGE_FILE_NAME "JoltFX-${PROJECT_VERSION}-beta.1")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
