# Ingagi Standard Library Catalog

This archive contains a proposed catalog of Ingagi standard-library modules, grouped by subsystem. It is a design catalog, not an implementation or finalized language specification.

## Suggested profiles

- `core`: language fundamentals, types, errors, strings, math, collections, memory, and serialization interfaces.
- `runtime`: application lifecycle, ECS, tasks, events, resource management, platform, and I/O.
- `game`: rendering, physics, audio, input, animation, navigation, UI, and gameplay utilities.
- `shader`: a restricted shader prelude, shader math, sampling, lighting, and GPU-stage intrinsics.
- `net`: transport, serialization, replication, prediction, rollback, and multiplayer facilities.
- `tools`: diagnostics, testing, profiling, editor integration, asset import, and build utilities.

## Design constraints

1. Keep the language core small.
2. Separate public API from backend implementations.
3. Restrict shader libraries to operations supported by the selected stage and target.
4. Lower native ECS constructs to explicit runtime contracts.
5. Make codecs, physics solvers, transports, and editor features optional.
6. Expose capability queries and feature flags.
7. Version public interfaces, ABI boundaries, and serialization schemas.

Each subsystem directory has a README listing its proposed modules and responsibilities. `modules.tsv` is the machine-readable catalog with columns `group`, `module`, and `description`.
