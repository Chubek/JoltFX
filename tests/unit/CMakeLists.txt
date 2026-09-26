add_executable(test_tillyz
    tilly/test_tillyz.c
)

target_link_libraries(test_tillyz PRIVATE tillyz)

add_executable(test_core
    core/test_engine.c
)

target_link_libraries(test_core PRIVATE jfx_core)
