include(GNUInstallDirs)

install(TARGETS
    jfx_core
    jfx_web_session
    jfx_mobile_player
    jfx_host_plugin_common
    jfx_ae_plugin
    jfx_premiere_plugin
    jfx_davinci_plugin
    joltfx_cli
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY core/include/jfx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(DIRECTORY frontends/web/include/jfx frontends/mobile/include/jfx
                  frontends/plugins/include/jfx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
    frontends/web/README.md
    frontends/mobile/README.md
    frontends/plugins/README.md
    docs/guides/phase5.md
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

set(CPACK_PACKAGE_NAME "JoltFX")
set(CPACK_PACKAGE_VENDOR "JoltFX")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "JoltFX-${PROJECT_VERSION}-beta.1")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
