cmake_minimum_required(VERSION 3.20)
project(JoltFXTintPlugin VERSION 1.0.0 LANGUAGES C)
find_package(JoltFXPluginSDK 1.0 CONFIG REQUIRED)
jfx_add_plugin(jfx_example_plugin SOURCES tint.c)
