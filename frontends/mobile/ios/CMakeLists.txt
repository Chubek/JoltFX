enable_language(OBJC)

add_library(jfx_ios_ui STATIC
    Sources/JFXMobilePlayerBridge.m
    Sources/JFXNLEViewController.m
    Sources/JFXColorViewController.m
    Sources/JFXCompositionViewController.m
    Sources/JFXModeling3DViewController.m
)
target_include_directories(jfx_ios_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Sources>
)
target_compile_options(jfx_ios_ui PRIVATE -fobjc-arc)
target_link_libraries(jfx_ios_ui PUBLIC jfx_mobile_player
    "-framework Foundation" "-framework UIKit" "-framework CoreGraphics"
    "-framework QuartzCore" "-framework AVFoundation")
set_target_properties(jfx_ios_ui PROPERTIES
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0")
set_project_warnings(jfx_ios_ui)

option(JFX_MOBILE_IOS_APP "Build the iOS NLE/color/composition application" ON)
if(JFX_MOBILE_IOS_APP)
    add_executable(jfx_ios MACOSX_BUNDLE App/main.m App/JFXAppDelegate.m)
    target_compile_options(jfx_ios PRIVATE -fobjc-arc)
    target_link_libraries(jfx_ios PRIVATE jfx_ios_ui)
    set_target_properties(jfx_ios PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "org.joltfx.mobile"
        MACOSX_BUNDLE_BUNDLE_NAME "JoltFX"
        MACOSX_BUNDLE_BUNDLE_VERSION "1"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/App/Info.plist.in"
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "14.0"
    )
    set_project_warnings(jfx_ios)
endif()
