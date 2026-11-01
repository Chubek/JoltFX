# SDK maintenance

- Record progress in the root `PROGRESS.md`.
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
