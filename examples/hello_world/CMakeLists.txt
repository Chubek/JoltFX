add_executable(example_hello_world
    main.c
)

target_link_libraries(example_hello_world PRIVATE jfx_core)
