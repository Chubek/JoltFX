# Changelog

All notable changes to JoltFX will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/).

## [Unreleased]

## [0.5.0-beta.1] - 2026-09-27

### Added
- Versioned native plugin host with lifecycle events and host adapter contracts
  for After Effects, Premiere Pro, and DaVinci Resolve.
- Dependency-free web player runtime and portable C WASM session bridge.
- Portable mobile playback core with touch gesture semantics.
- Frame timing metrics, a repeatable engine microbenchmark, beta install rules,
  and public-beta documentation.

### Changed
- Pipeline execution now skips redundant bytecode validation for immutable,
  already-validated pipeline stages while preserving validation at public API
  boundaries.

### Known limitations
- Host application SDK adapters, Emscripten export glue, Android/iOS wrappers,
  and native GPU implementations for Metal/D3D12/WebGPU remain release-blocking
  work for a general-availability release.

### Added
- Initial project structure
- Core engine API design
- Tilly runtime foundation
- JoltScript language specification
