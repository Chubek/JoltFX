add_subdirectory(common)

if(JFX_EXT_LUA)
    add_subdirectory(lua)
endif()
if(JFX_EXT_MRUBY)
    add_subdirectory(mruby)
endif()
# QuickJS and MicroPython are vendored (third_party/quickjs,
# third_party/micropython) but have no adapter yet; enabling these options is
# therefore a configure error rather than a silent no-op.
if(JFX_EXT_QUICKJS)
    message(FATAL_ERROR
        "JFX_EXT_QUICKJS is not implemented: there is no extif/quickjs adapter. "
        "See PROGRESS.md for the remaining extension work.")
endif()
if(JFX_EXT_PYTHON)
    message(FATAL_ERROR
        "JFX_EXT_PYTHON is not implemented: there is no extif/python adapter. "
        "See PROGRESS.md for the remaining extension work.")
endif()
