add_subdirectory(unit)
add_subdirectory(integration)

add_test(NAME test_tillyz COMMAND test_tillyz)
add_test(NAME test_core COMMAND test_core)
