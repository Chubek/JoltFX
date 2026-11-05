# SDK maintenance

- Record progress in the root `PROGRESS.md`.
- `FUTURE_OF_SDK.md` is the working roadmap: consult it before starting SDK
  work, and keep its "Current state" / "In progress" / "Next" sections current
  as items land or change.
- Facets adapt canonical public headers; do not duplicate ABI declarations or
  leak private/vendor types. Ingagi and Joltscript headers are copied at install
  time from their own subsystems. Their runtimes are separate link dependencies.
- Keep `sdk_facets`, adapter headers, README inventory and `docs/facets.md` aligned.
  Additive package components require a minor SDK version bump; underlying ABI
  versions remain owned by their subsystems.
- Keep installed targets relocatable and standalone configuration independent of
  Go, shader tools and the full engine build. External header roots are explicit.
- Validate packaging with `cmake -DSOURCE_DIR=<repo> -DTEST_DIR=<scratch>
  -P tests/integration/sdk/test_install.cmake` from the repository root. This
  compiles each installed facet in C11/C++17 and checks required components.
- Document implemented capabilities, runtime/build dependencies and profile
  limitations from canonical headers and language references.
- OpenFX 1.2 is an inspection-only component. Keep the uninstalled execution
  draft distinct from implemented symbols; see `docs/openfx.md`. Run standalone
  fixture tests with `JOLTFX_SDK_BUILD_TESTS=ON` when changing the host.
