plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

android {
    namespace = "org.joltfx.mobile"
    compileSdk = 35
    buildToolsVersion = "35.0.0"
    ndkVersion = "27.2.12479018"

    defaultConfig {
        applicationId = "org.joltfx.mobile"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "0.6.0"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake {
                targets += "jfx_android_jni"
                arguments += listOf(
                    "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
                    "-DANDROID_SUPPORT_FLEXIBLE_PAGE_SIZES=ON",
                    "-DJFX_FRONTEND_MOBILE=ON",
                    "-DJFX_FRONTEND_DESKTOP=OFF",
                    "-DJFX_FRONTEND_CLI=OFF",
                    "-DJFX_FRONTEND_WEB=OFF",
                    "-DJFX_PLUGIN_HOST_BRIDGES=OFF",
                    "-DJFX_BACKEND_VULKAN=ON",
                    "-DJFX_BACKEND_METAL=OFF",
                    "-DJFX_BACKEND_D3D12=OFF",
                    "-DJFX_BACKEND_WEBGPU=OFF",
                    "-DJFX_EXT_LUA=OFF",
                    "-DJFX_EXT_MRUBY=OFF",
                    "-DJFX_EXT_QUICKJS=OFF",
                    "-DJFX_EXT_PYTHON=OFF",
                    "-DJFX_COLOR_OCIO=OFF",
                    "-DJFX_VIDEO_FFMPEG=ON",
                    "-DJFX_MEDIA_FFMPEG_BUNDLED=ON",
                    "-DJOLTFX_BUILD_TESTS=OFF",
                    "-DJOLTFX_BUILD_EXAMPLES=OFF"
                )
            }
        }
    }

    externalNativeBuild {
        cmake {
            path = file("../../../../CMakeLists.txt")
            version = "3.22.1"
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
    kotlinOptions { jvmTarget = "17" }
}
