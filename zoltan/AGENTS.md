# AGENTS.md — Zoltan

## Overview

Zoltan is the JoltFX command-line interface and development toolchain. It provides project scaffolding, kernel compilation, live preview, hot-reload, package management, and deployment export. This guide covers contributions to Zoltan's codebase, conventions, and release process.

Zoltan is implemented in **Rust** (stable, 1.70+) and uses the JoltFX Core engine via FFI.

---

## Repository Layout

```
zoltan/
  src/
    main.rs              # CLI entry point and argument parsing (clap)
    context.rs           # Shared application context
    commands/
      new.rs             # Project scaffolding
      build.rs           # Kernel and effect compilation
      watch.rs           # File watcher and hot-reload server
      preview.rs         # Live preview window
      package.rs         # Dependency management
      export.rs          # Deployment artifact generation
      run.rs             # Execute project directly
      doctor.rs          # Environment diagnostics
    core/
      compiler.rs        # Joltscript compiler bridge (FFI to joltfx.h)
      project.rs         # Project file (zoltan.toml) parsing
      registry.rs        # Effect and kernel registry client
      validation.rs      # Lint and verify passes
      fs.rs              # File system utilities (watched paths, globs)
    preview/
      server.rs          # WebSocket hot-reload server
      renderer.rs        # Preview window renderer (GPU backend via jfx_context_t)
      ui.rs              # Parameter tweaking UI (egui)
    export/
      targets.rs         # Export target definitions
      bundle.rs          # Artifact bundling and minification
      signing.rs         # Artifact signing and verification
  templates/
    basic/               # Default project template
    advanced/            # Template with effect graph
    audio-reactive/      # Template with audio analysis
    transition/          # Template for transitions
  tests/
    integration/         # End-to-end CLI tests
    fixtures/            # Sample projects for testing
    snapshots/           # Expected output snapshots
  benches/               # Performance benchmarks (compiler bridge, hot-reload)
  docs/
    cli-reference.md     # Generated command documentation
    project-format.md    # zoltan.toml schema documentation
    export-guide.md      # Export target documentation
    migration/           # Migration guides for breaking changes
```

---

## Ownership Rules

| Area                      | Gate Before Merge                          |
|---------------------------|--------------------------------------------|
| `commands/`               | One reviewer sign-off                      |
| `core/compiler.rs`        | Compiler team review (two sign-offs)       |
| `core/project.rs`         | One reviewer + migration test              |
| `preview/renderer.rs`     | Platform lead + perf sign-off              |
| `export/`                 | Export format owner + security review      |
| `templates/`              | UX review from design team                 |
| `benches/`                | Perf owner review for changes              |
| Everything else           | One reviewer sign-off                      |

For breaking CLI changes (flag renames, output format changes), post in `#joltfx-tooling` before opening a PR.

---

## Build and Toolchain

Zoltan is written in Rust and targets **stable Rust 1.70+**. Do not use nightly features without an explicit architecture decision.

```bash
# Build
cargo build --release

# Test
cargo test --all-features

# Lint
cargo clippy --all-targets --all-features -- -D warnings

# Format
cargo fmt --all --check

# Benchmarks
cargo bench --all-features

# Documentation
cargo doc --all-features --no-deps
```

CI rejects submissions with clippy warnings, unformatted code, or failing tests.

**Platform requirements:**
- **Windows:** MSVC toolchain (for GPU backend linking)
- **macOS:** Xcode command-line tools
- **Linux:** `libgtk-4-dev`, `libvulkan-dev`, `libssl-dev` (for preview window and registry TLS)

---

## Command Structure

Each command lives in `src/commands/<name>.rs` and implements the `Command` trait:

```rust
pub trait Command {
    fn name(&self) -> &'static str;
    fn description(&self) -> &'static str;
    fn run(&self, ctx: &mut Context, args: &ArgMatches) -> Result<()>;
    fn args(&self) -> CommandArgs;  // clap Arg configuration
}
```

Commands are registered in `src/main.rs`. New commands must:
1. Add a module in `src/commands/`.
2. Implement the `Command` trait.
3. Register in `main.rs` command dispatch table.
4. Add integration test in `tests/integration/`.
5. Document in `docs/cli-reference.md`.

### Global Options

All commands support:
- `--config <path>` — Path to zoltan.toml (default: current dir)
- `--verbose` / `-v` — Increase verbosity (up to `-vvv`)
- `--quiet` / `-q` — Suppress non-error output
- `--no-color` — Disable ANSI colors
- `--help` / `-h` — Show command-specific help

---

## Project File (`zoltan.toml`)

Every Zoltan project has a `zoltan.toml` at its root:

```toml
[project]
name = "my-effect"
version = "1.0.0"
author = "Your Name"
description = "A cool motion graphics effect"
license = "MIT"
repository = "https://github.com/user/my-effect"

[build]
target = "effect"           # effect | plugin | library | app
output = "dist/"
optimize = true             # Enable dead-code elimination, constant folding
debug_symbols = false       # Include debug info in output

[dependencies]
joltfx-stdlib = "2.0"
joltfx-particles = "1.3"

[preview]
width = 1920
height = 1080
fps = 60
backend = "vulkan"          # vulkan | metal | d3d12 | webgpu | auto
hot_reload = true           # Enable file watching and hot-reload
port = 7072                 # WebSocket port for hot-reload server

[export]
default_target = "standalone"
targets = ["standalone", "web", "plugin"]

[export.standalone]
platforms = ["windows", "macos", "linux"]
strip_debug = true
minify = true

[export.web]
wasm_opt_level = "3"
compress = true

[export.plugin]
formats = ["ae", "premiere", "davinci"]
```

Parsing happens in `src/core/project.rs`. Adding a new section or field requires:
- Schema update in `project.rs` (using `serde` with `toml`)
- Migration logic for existing projects (use `version` field)
- Update to template files in `templates/`
- Documentation in `docs/project-format.md`

**Do not break existing project files.** Use `Option<T>` and provide sensible defaults for new features.

---

## Compiler Bridge

Zoltan delegates Joltscript compilation to the engine's Glue Layer via FFI. The bridge lives in `src/core/compiler.rs` and wraps the C API declared in `joltfx.h`.

```rust
pub fn compile_kernel(
    source: &Path,
    output: &Path,
    options: &CompileOptions,
) -> Result<CompilationResult> {
    // FFI call to jfx_compile_kernel
    // Error mapping and diagnostics formatting
}
```

### Rules

- **Never expose raw C pointers** to Rust callers; wrap them in safe types.
- **Map all `jfx_result_t` codes** to Rust `Result<T, ZoltanError>`.
- **Compiler diagnostics** must include file, line, column, and snippet context.
- **Always free C-allocated memory** before returning or on early error paths (use RAII wrappers).
- Adding a new compilation option requires updating both the Rust struct and the corresponding C struct in the engine's public API.

### CompileOptions

```rust
pub struct CompileOptions {
    pub target: CompileTarget,        // C, Rust, Python, Go, SPIR-V, Native
    pub optimize: OptLevel,           // None, Basic, Aggressive
    pub debug_info: bool,
    pub emit_metadata: bool,
    pub kernel_name: Option<String>,  // Compile specific kernel
}
```

---

## Live Preview

The preview window runs a separate renderer thread that polls the hot-reload server. Preview state lives in `src/preview/`.

```rust
pub struct PreviewWindow {
    renderer: Renderer,
    ui: UiState,
    server: HotReloadServer,
    project: Arc<Project>,
}
```

**Constraints:**
- Preview must not block the main thread. File watching and recompilation happen asynchronously and push updates to the renderer via a channel.
- The renderer uses the engine's GPU path. Preview code must **never directly call OpenGL/Vulkan/Metal**; use the `jfx_context_t` API exclusively.
- UI parameter tweaking sends messages back to the server, which writes updated values to the project's parameter file and triggers a recompile.

### Renderer Architecture

```
Main Thread                    Renderer Thread
     │                              │
     │  File change detected        │
     ├─────────────────────────────►│
     │                              │  Compile kernel (async)
     │                              │  Update GPU resources
     │                              │  Render frame
     │◄─────────────────────────────┤  Frame ready callback
     │  Update UI                   │
```

---

## Hot-Reload Server

The hot-reload server is a WebSocket server on `localhost:7072` by default. It broadcasts file-change events and compilation results to connected clients (preview window, IDE extensions).

### Protocol Messages (JSON)

```json
{
  "type": "file_changed",
  "path": "src/kernels/blur.jolt",
  "timestamp": 1672531200
}

{
  "type": "compilation_started",
  "kernel": "blur"
}

{
  "type": "compilation_complete",
  "success": true,
  "errors": [],
  "duration_ms": 45
}

{
  "type": "compilation_error",
  "kernel": "blur",
  "errors": [
    {"file": "blur.jolt", "line": 12, "column": 5, "message": "undefined variable 'radius'"}
  ]
}

{
  "type": "parameter_update",
  "kernel": "blur",
  "param": "radius",
  "value": 5.0
}

{
  "type": "frame_rendered",
  "frame_number": 120,
  "duration_ms": 16.67
}
```

Server implementation lives in `src/preview/server.rs`. **Do not add blocking operations in the message handler;** spawn tasks for long-running work.

---

## Export Targets

Export generates deployment-ready artifacts. Targets are defined in `src/export/targets.rs`:

```rust
pub struct ExportTarget {
    pub name: &'static str,
    pub platform: Platform,
    pub format: OutputFormat,
    pub minify: bool,
    pub strip_debug: bool,
    pub sign: bool,
}
```

### Current Targets

| Target | Description | Platforms |
|--------|-------------|-----------|
| `standalone` | Single-file effect binary | Windows, macOS, Linux |
| `plugin` | FCP/Premiere/AE/DaVinci plugin | Per-host |
| `web` | WebGL/WebGPU bundle for browser | WASM |
| `library` | Static/dynamic library with C header | All |
| `app` | Full standalone application | Windows, macOS, Linux |

### Adding a New Target

1. Define the target in `targets.rs`.
2. Implement bundling logic in `bundle.rs`.
3. Add platform-specific linking/packaging in `export/<target>/`.
4. Update `docs/export-guide.md`.
5. Add integration test that exports and validates the artifact.

**Security:** Export artifacts must pass security scan (no embedded credentials, no world-writable files). The CI export lane runs artifact validation automatically.

---

## Package Management

Zoltan fetches dependencies from the JoltFX registry (`registry.joltfx.io` by default). Registry client lives in `src/core/registry.rs`.

```rust
pub async fn fetch_package(
    name: &str,
    version: &Version,
) -> Result<PackageManifest> {
    // HTTP fetch, signature verification, cache
}
```

### Cache

- Packages cached in `~/.zoltan/cache/` (respects `XDG_CACHE_HOME`).
- Cache eviction: LRU with 10 GB default limit (configurable via `ZOLTAN_CACHE_SIZE`).
- Index stored as SQLite for fast lookups.

### Resolution

- Dependency resolution uses semantic versioning (`semver` crate).
- Do not introduce custom version schemes; stay compatible with Cargo-style resolution.
- Lock file: `zoltan.lock` (generated on `zoltan build`, committed to VCS).

### Verification

- All fetched packages **must be signature-verified** before extraction.
- The public key is embedded in the Zoltan binary at build time (rotated annually).
- Verification uses Ed25519 signatures over package tarball hash.

---

## Templates

Project templates live in `templates/`. Each template is a directory with:
- `zoltan.toml` — project file
- `src/` — kernel and effect source
- `README.md` — template-specific instructions

Templates are embedded in the Zoltan binary at compile time using `include_dir!`.

**Requirements for new templates:**
- Use only stable Joltscript features (no experimental syntax).
- Include inline comments explaining key concepts.
- Compile cleanly on all platforms.
- Pass UX review (post in `#joltfx-design` before merging).

The `basic` template is the default for `zoltan new`; do not change its structure without updating onboarding documentation.

---

## Error Reporting

Zoltan errors use `miette` for pretty-printed diagnostics with source snippets:

```rust
use miette::{Diagnostic, SourceSpan};
use thiserror::Error;

#[derive(Debug, Diagnostic, Error)]
#[error("undefined variable `{name}`")]
#[diagnostic(code(zoltan::undefined_variable))]
pub struct UndefinedVariable {
    pub name: String,
    #[label("not found in this scope")]
    pub span: SourceSpan,
    #[help]
    pub help: Option<String>,
}
```

### Rules

- Every error must have a unique code (`zoltan::<category>::<name>`).
- Include source spans when the error relates to user code.
- Provide actionable help text when possible.
- Do not expose internal panics to users; use `Result` and map errors.

### Error Categories

| Category | Examples |
|----------|----------|
| `zoltan::project::` | Invalid TOML, missing fields, migration failures |
| `zoltan::compile::` | Syntax errors, type errors, unresolved symbols |
| `zoltan::fs::` | File not found, permission denied, glob errors |
| `zoltan::registry::` | Network errors, signature verification, version conflicts |
| `zoltan::export::` | Packaging failures, signing errors, format issues |
| `zoltan::preview::` | WebSocket errors, renderer initialization |

---

## Testing

```bash
# Unit tests (run with cargo test)
cargo test --lib

# Integration tests
cargo test --test integration

# Specific fixture test
cargo test --test integration test_basic_template

# Benchmarks
cargo bench
```

**Test organization:**
- Unit tests go next to the code they test (`#[cfg(test)] mod tests { ... }`).
- Integration tests live in `tests/integration/` and use fixture projects in `tests/fixtures/`.
- Snapshots in `tests/snapshots/` for output comparison.

**Minimum test coverage for a new command:**
- Happy path with valid project
- Invalid project file handling
- Missing dependency handling
- Compilation error propagation
- Output artifact validation

**Platform-specific tests:**
- Preview and export tests run only on platforms that support them (gated by `#[cfg(target_os = "...")]`).
- Do not skip tests on unsupported platforms; use conditional compilation instead.

---

## Release Process

Zoltan releases follow semantic versioning and are published to crates.io and the JoltFX releases page.

1. Bump version in `Cargo.toml`.
2. Update `CHANGELOG.md` with notable changes (use `cargo-release` or manual).
3. Run full test suite on all platforms: `cargo test --all-features --target <platform>`.
4. Build release binaries: `cargo build --release --target <platform>`.
5. Sign binaries with the release key (stored in 1Password, `joltfx-release`).
6. Tag release: `git tag -a v<version> -m "Release <version>"`.
7. Push tag: `git push origin v<version>`.
8. CI builds and publishes to crates.io and GitHub Releases automatically.
9. Announce in `#joltfx-announcements`.

**Breaking changes** (major version bumps) require migration guide in `docs/migration/`.

---

## PR Checklist

- [ ] Builds clean with `cargo clippy --all-targets --all-features -- -D warnings`
- [ ] Formatted with `cargo fmt --all`
- [ ] All tests pass on local platform (`cargo test --all-features`)
- [ ] Integration test added for new command or changed behavior
- [ ] CLI reference updated if command flags changed
- [ ] Template updated if project format changed
- [ ] Migration logic added if breaking existing projects
- [ ] Export artifact validated if export code changed
- [ ] Gate requirement met (see Ownership Rules)
- [ ] No hardcoded paths or credentials
- [ ] Error codes follow `zoltan::<category>::<name>` convention
- [ ] Benchmarks run for compiler bridge / hot-reload changes

---

## Common Mistakes

### Blocking the Preview Thread
Compilation and file I/O must happen on a separate task. The preview renderer runs at 60fps; any blocking call causes dropped frames.

### Not Freeing C Memory
Every call to the compiler bridge that returns a pointer must have a corresponding free before returning or on error paths. Use RAII wrappers to avoid leaks.

### Breaking Existing Projects
Adding a required field to `zoltan.toml` without a default breaks every existing project. Use `Option<T>` and provide a sensible default.

### Exposing Internal Errors
Users should never see "thread panicked" or raw C error codes. Map all errors to user-facing diagnostics with source context.

### Skipping Signature Verification
Every package fetched from the registry must be verified before extraction. Skipping verification is a critical security issue.

### Hardcoding Registry URL
Use the `JOLTFX_REGISTRY` environment variable or `zoltan.toml` override. Do not assume `registry.joltfx.io` is always available.

### Ignoring Platform Differences
File paths, line endings, and case sensitivity differ across platforms. Use `std::path` and test on Windows/macOS/Linux.

---

## Contacts

- **CLI and tooling**: `#joltfx-tooling`
- **Compiler bridge and FFI**: `#joltfx-compiler`
- **Preview and hot-reload**: `#joltfx-preview`
- **Export targets and packaging**: `#joltfx-export`
- **Registry and package management**: `#joltfx-registry`
- **Templates and UX**: `#joltfx-design`

For questions about this guide or Zoltan architecture, ask in `#joltfx-tooling`.