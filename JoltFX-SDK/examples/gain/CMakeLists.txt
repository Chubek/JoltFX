cmake_minimum_required(VERSION 3.20)
project(JoltFXGainExample LANGUAGES C)
find_package(JoltFXSDK 1 REQUIRED CONFIG)
joltfx_sdk_add_plugin(joltfx_gain gain.c)
