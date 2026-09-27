# JoltFX Roadmap

## Phase 1: Foundation (Q1 2027)
- [ ] TillyZ bootstrap implementation
- [ ] Tilly runtime (allocator, logger, module system)
- [ ] Core engine skeleton (init/shutdown/basic API)
- [ ] Basic CMake build system

## Phase 2: Core Functionality (Q2 2027)
- [x] JoltScript execution and glue layers
- [x] Vulkan backend implementation (JBC1→GLSL codegen + device dispatch verified on RX 580; pipeline caching and async submit remain open, see PROGRESS.md)
- [x] Essential kernel library (10+ effects)
- [x] Zoltan compiler MVP

## Phase 3: Integration (Q3 2027)
- [x] CLI frontend (`compile`/`verify`/`effects`/`info`/`render` + `version`/`help`/`run`, PPM render path)
- [x] Unit and integration tests (9/9 CTest: 7 existing + `cli_integration` + `example_hello_world`)
- [x] CI/CD pipeline (build-test matrix, ASan/UBSan, Zoltan fmt/clippy/test, CLI smoke)
- [x] Hello World example (engine + compile + pipeline run + PPM-equivalent pixel check)

## Phase 4: Expansion (Q4 2027)
- [ ] Additional backends (Metal, D3D12, WebGPU)
- [ ] Desktop GUI frontend
- [ ] Extension language bindings (Lua, mruby)
- [ ] Comprehensive documentation

## Phase 5: Production (2028)
- [ ] Web and mobile frontends
- [ ] Plugin system (After Effects, Premiere, DaVinci)
- [ ] Performance optimization
- [ ] Public beta release

