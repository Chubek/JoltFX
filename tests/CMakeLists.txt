add_subdirectory(unit)
add_subdirectory(integration)
add_subdirectory(perf)

# Test registration lives next to each target in tests/unit and
# tests/integration, so a target and its add_test() cannot drift apart.
