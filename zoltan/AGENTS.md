```markdown
# AGENTS.md — Zoltan

## Overview

Zoltan is the JoltFX command-line interface and development toolchain. It provides
project scaffolding, kernel compilation, live preview, hot-reload, package
management, and deployment export. This guide covers contributions to Zoltan's
codebase, conventions, and release process.

---

## Repository Layout

```
zoltan/
  src/
    main.rs           # CLI entry point and argument parsing
    commands/
      new.rs          # project scaffolding
      build.rs        # kernel and effect compilation
      watch.rs        # file watcher and hot-reload server
      preview.rs      # live preview window
      package.rs      # dependency management
      export.rs       # deployment artifact generation
    core/
      compiler.rs     # Joltscript compiler bridge
      project.rs      # project file (zoltan.toml) parsing
      registry.rs     # effect and kernel registry client
      validation.rs   # lint and verify passes
    preview/
      server.rs       # WebSocket hot-reload server
      renderer.rs     # preview window renderer (GPU backend)
      ui.rs           # parameter tweaking UI
    export/
      targets.rs      # export target definitions
      bundle.rs       # artifact bundling and minification
  templates/
    basic/            # default project template
    advanced/         # template with effect graph
  tests/
    integration/      # end-to-end CLI tests
    fixtures/         # sample projects for testing
  docs/
    cli-reference.md  # generated command documentation

---

## Ownership Rules

| Area                  | Gate before merge                        |
|-----------------------|------------------------------------------|
| `commands/`           | One reviewer sign-off                    |
| `core/compiler.rs`    | Compiler team review                     |
| `preview/renderer.rs` | Platform lead + perf sign-off            |
| `export/`             | Export format owner + security review    |
| Templates             | UX review from design team               |
| Everything else       | One reviewer sign-off                    |

For breaking CLI changes (flag renames, output format changes), post in
`#joltfx-tooling` before opening a PR.

---

## Build and Toolchain

Zoltan is written in Rust and targets stable Rust 1.70+. Do not use nightly
features without an explicit architecture decision.

sh
cargo build --release
cargo test --all-features
cargo clippy -- -D warnings
cargo fmt -- --check

CI rejects submissions with clippy warnings or unformatted code.

Platform requirements:
- **Windows:** MSVC toolchain (for GPU backend linking)
- **macOS:** Xcode command-line tools
- **Linux:** `libgtk-3-dev`, `libvulkan-dev` (for preview window)

---

## Command Structure

Each command lives in `src/commands/<name>.rs` and implements the `Command` trait:

rust
pub trait Command {
    fn name(&self) -> &str;
    fn run(&self, ctx: &mut Context, args: &ArgMatches) -> Result<()>;
}

Commands are registered in `src/main.rs`. New commands must:
1. Add a module in `src/commands/`.
2. Implement the `Command` trait.
3. Register in `main.rs` command dispatch table.
4. Add integration test in `tests/integration/`.
5. Document in `docs/cli-reference.md`.

---

## Project File (`zoltan.toml`)

Every Zoltan project has a `zoltan.toml` at its root:

toml
[project]
name = "my-effect"
version = "1.0.0"
author = "Your Name"

[build]
target = "effect"       # effect | plugin | library
output = "dist/"

[dependencies]
joltfx-stdlib = "2.0"

[preview]
width = 1920
height = 1080
fps = 60

Parsing happens in `src/core/project.rs`. Adding a new section or field requires:
- Schema update in `project.rs`
- Migration logic for existing projects (use version field)
- Update to template files in `templates/`
- Documentation in `docs/project-format.md`

Do not break existing project files. Use optional fields and defaults for new
features.

---

## Compiler Bridge

Zoltan delegates Joltscript compilation to the engine's Glue Layer via FFI. The
bridge lives in `src/core/compiler.rs` and wraps the C API declared in
`joltfx.h`.

rust
pub fn compile_kernel(
    source: &Path,
    output: &Path,
    options: &CompileOptions,
) -> Result<CompilationResult> {
    // FFI call to jfx_compile_kernel
    // Error mapping and diagnostics formatting
}

Rules:
- Never expose raw C pointers to Rust callers; wrap them in safe types.
- Map all `jfx_result_t` codes to Rust `Result<T, ZoltanError>`.
- Compiler diagnostics must include file, line, column, and snippet context.
- Always free C-allocated memory before returning or on early error paths.

Adding a new compilation option requires updating both the Rust struct and the
corresponding C struct in the engine's public API.

---

## Live Preview

The preview window runs a separate renderer thread that polls the hot-reload
server. Preview state lives in `src/preview/`.

rust
pub struct PreviewWindow {
    renderer: Renderer,
    ui: UiState,
    server: HotReloadServer,
}

Preview must not block the main thread. File watching and recompilation happen
asynchronously and push updates to the renderer via a channel.

The renderer uses the engine's GPU path. Preview code must never directly call
OpenGL/Vulkan/Metal; use the `jfx_context_t` API exclusively.

UI parameter tweaking sends messages back to the server, which writes updated
values to the project's parameter file and triggers a recompile.

---

## Hot-Reload Server

The hot-reload server is a WebSocket server on `localhost:7072` by default. It
broadcasts file-change events and compilation results to connected clients
(preview window, IDE extensions).

Protocol messages are JSON:

json
{
  "type": "file_changed",
  "path": "src/kernels/blur.jolt",
  "timestamp": 1672531200
}

{
  "type": "compilation_complete",
  "success": true,
  "errors": []
}

{
  "type": "parameter_update",
  "kernel": "blur",
  "param": "radius",
  "value": 5.0
}

Server implementation lives in `src/preview/server.rs`. Do not add blocking
operations in the message handler; spawn tasks for long-running work.

---

## Export Targets

Export generates deployment-ready artifacts. Targets are defined in
`src/export/targets.rs`:

rust
pub struct ExportTarget {
    pub name: &'static str,
    pub platform: Platform,
    pub format: OutputFormat,
    pub minify: bool,
    pub strip_debug: bool,
}

Current targets:
- `standalone` — Single-file effect binary (Windows/macOS/Linux)
- `plugin` — FCP/Premiere/AE plugin
- `web` — WebGL/WebGPU bundle for browser
- `library` — Static/dynamic library with C header

Adding a new target requires:
1. Define the target in `targets.rs`.
2. Implement bundling logic in `bundle.rs`.
3. Add platform-specific linking/packaging in `export/<target>/`.
4. Update `docs/export-guide.md`.
5. Add integration test that exports and validates the artifact.

Export artifacts must pass security scan (no embedded credentials, no world-writable
files). The CI export lane runs artifact validation automatically.

---

## Package Management

Zoltan fetches dependencies from the JoltFX registry (registry.joltfx.io by
default). Registry client lives in `src/core/registry.rs`.

rust
pub async fn fetch_package(
    name: &str,
    version: &Version,
) -> Result<PackageManifest> {
    // HTTP fetch, signature verification, cache
}

Packages are cached in `~/.zoltan/cache/`. Cache eviction is LRU with a 10GB
default limit.

Dependency resolution uses semantic versioning. Do not introduce custom version
schemes; stay compatible with `semver` crate.

All fetched packages must be signature-verified before extraction. The public
key is embedded in the Zoltan binary at build time.

---

## Templates

Project templates live in `templates/`. Each template is a directory with:
- `zoltan.toml` — project file
- `src/` — kernel and effect source
- `README.md` — template-specific instructions

Templates are embedded in the Zoltan binary at compile time using `include_dir!`.
New templates must:
- Use only stable Joltscript features (no experimental syntax).
- Include inline comments explaining key concepts.
- Compile cleanly on all platforms.
- Pass UX review (post in `#joltfx-design` before merging).

The `basic` template is the default for `zoltan new`; do not change its structure
without updating onboarding documentation.

---

## Error Reporting

Zoltan errors use `miette` for pretty-printed diagnostics with source snippets:

rust
use miette::{Diagnostic, SourceSpan};

#[derive(Debug, Diagnostic, thiserror::Error)]
#[error("undefined variable `{name}`")]
#[diagnostic(code(zoltan::undefined_variable))]
pub struct UndefinedVariable {
    pub name: String,
    #[label("not found in this scope")]
    pub span: SourceSpan,
    #[help]
    pub help: Option<String>,
}

Rules:
- Every error must have a unique code (`zoltan::category::name`).
- Include source spans when the error relates to user code.
- Provide actionable help text when possible.
- Do not expose internal panics to users; use `Result` and map errors.

---

## Testing

Unit tests go next to the code they test (`#[cfg(test)] mod tests { ... }`).
Integration tests live in `tests/integration/` and use the fixture projects in
`tests/fixtures/`.

Minimum test coverage for a new command:
- Happy path with valid project
- Invalid project file handling
- Missing dependency handling
- Compilation error propagation
- Output artifact validation

Preview and export tests run only on platforms that support them (gated by
`#[cfg(target_os = "...")]`). Do not skip tests on unsupported platforms; use
conditional compilation instead.

Performance-sensitive paths (compiler bridge, hot-reload) have benchmark tests
in `benches/`. Run `cargo bench` before merging changes to these areas.

---

## Release Process

Zoltan releases follow semantic versioning and are published to crates.io and
the JoltFX releases page.

1. Bump version in `Cargo.toml`.
2. Update `CHANGELOG.md` with notable changes.
3. Run full test suite on all platforms: `cargo test --all-features --target <platform>`.
4. Build release binaries: `cargo build --release --target <platform>`.
5. Sign binaries with the release key (stored in 1Password, `joltfx-release`).
6. Tag release: `git tag -a v<version> -m "Release <version>"`.
7. Push tag: `git push origin v<version>`.
8. CI builds and publishes to crates.io and GitHub Releases automatically.
9. Announce in `#joltfx-announcements`.

Breaking changes (major version bumps) require migration guide in `docs/migration/`.

---

## PR Checklist

- [ ] Builds clean with `cargo clippy -- -D warnings`
- [ ] Formatted with `cargo fmt`
- [ ] All tests pass on local platform
- [ ] Integration test added for new command or changed behavior
- [ ] CLI reference updated if command flags changed
- [ ] Template updated if project format changed
- [ ] Migration logic added if breaking existing projects
- [ ] Export artifact validated if export code changed
- [ ] Gate requirement met (see Ownership Rules)
- [ ] No hardcoded paths or credentials
- [ ] Error codes follow `zoltan::<category>::<name>` convention

---

## Common Mistakes

**Blocking the preview thread.** Compilation and file I/O must happen on a
separate task. The preview renderer runs at 60fps; any blocking call causes
dropped frames.

**Not freeing C memory.** Every call to the compiler bridge that returns a
pointer must have a corresponding free before returning or on error paths. Use
RAII wrappers to avoid leaks.

**Breaking existing projects.** Adding a required field to `zoltan.toml` without
a default breaks every existing project. Use `Option<T>` and provide a sensible
default.

**Exposing internal errors.** Users should never see "thread panicked" or raw C
error codes. Map all errors to user-facing diagnostics with source context.

**Skipping signature verification.** Every package fetched from the registry
must be verified before extraction. Skipping verification is a critical security
issue.

**Hardcoding registry URL.** Use the `JOLTFX_REGISTRY` environment variable or
`zoltan.toml` override. Do not assume `registry.joltfx.io` is always available.

---

## Contacts

- CLI and tooling: `#joltfx-tooling`
- Compiler bridge and FFI: `#joltfx-compiler`
- Preview and hot-reload: `#joltfx-preview`
- Export targets and packaging: `#joltfx-export`
- Registry and package management: `#joltfx-registry`

For questions about this guide or Zoltan architecture, ask in `#joltfx-tooling`.