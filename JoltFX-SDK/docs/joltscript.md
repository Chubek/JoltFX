# Joltscript in the SDK

Joltscript is the S-expression language used to author JoltFX kernels. SDK 1.1
ships the canonical compiler, VM, typed-value, Glue binding, pipeline and
image/audio-task headers behind `<joltfx/sdk/joltscript.h>`.

## Execution profiles

| Profile | Implemented contract | Execution |
| --- | --- | --- |
| JBC1 version 1 | Bounded f32 stack programs, locals and forward-only lazy conditionals; up to 32 inputs/four outputs | CPU interpreter, optional SLJIT, backend-specific supported lowering |
| JBC1 version 2 | Typed primitives, annotations/inference/casts, loops, functions, recursion, lexical closures and immutable collections | Budgeted typed CPU VM, including Vulkan CPU fallback |
| CPU image source | Separate bounded image language with sampling and image parameters, image ABI 0.1.0 | `jolt_image_kernel_compile` plus synchronous image-task execution |

The implemented language checkpoint is Phases 1–2, **Execution ABI 0.9**.
Glue bindings/media use **Glue ABI 0.4.0**. These version numbers are independent
of SDK 1.1, the plugin ABI and the JBC1 container version.

Typed imports expose six built-in modules with 262 exports: `jolt.math`,
`jolt.graphics`, `jolt.geometry`, `jolt.time`, `jolt.collections` and
`jolt.strings`. Imports resolve the embedded catalogue, not arbitrary filesystem
modules. The library includes exact integers, Unicode, vectors/lists/maps/sets,
geometry, color, easing and PCRE2-backed regular expressions.

Hygienic macros, guaranteed tail-call optimization, user FFI forms, general user
modules, `jolt.io`, native C/Rust/Python/Go source generation, and typed
shader/browser export remain design targets. C and Rust currently emit matching
JBC1; Zoltan's HTML/WAT lowering accepts version 1.

## Choose an integration surface

- **Engine RGBA kernels:** `jfx_kernel_load_source`, `jfx_kernel_execute` and
  `jfx_kernel_destroy`, linked with `JoltFX::jfx_core`. The engine API requires
  four RGBA outputs and supplies backend dispatch and engine ownership.
- **Direct compiler/VM:** `jolt_compile`, `jolt_program_run_values` and
  `jolt_vm_create`. Link `JoltFX::joltscript_glue`, which carries Execution and
  its dependencies. VM-only bytecode hosts may link `JoltFX::joltscript_execution`.
- **Image-effect plugins:** register image source through the existing plugin
  host-service table using `JoltFXSDK::plugins`; the host compiles and executes
  the bounded CPU image profile. These modules do not link the compiler directly.

## Build and run the SDK example

With the full JoltFX runtime and SDK installed:

```sh
cmake -S JoltFX-SDK/examples/joltscript -B build/sdk-joltscript-example \
  -DCMAKE_PREFIX_PATH="/path/to/sdk;/path/to/joltfx"
cmake --build build/sdk-joltscript-example
ctest --test-dir build/sdk-joltscript-example --output-on-failure
```

The example compiles `(defkernel answer [] (+ 40i64 2i64))`, executes with the
tagged-value API and checks an exact integer result of 42. All failure paths
release the program and VM. The full runtime build needs Python 3 for stdlib
catalogue generation and CMake for vendored PCRE2; SDK-only packaging does not.

## Values, budgets and ownership

`jolt_compile` accepts NUL-terminated source up to 1 MiB and reports source
line/column diagnostics. Compiled programs own their bytecode. Destroy programs
with `jolt_program_destroy`; destroy VMs with `jolt_vm_destroy`.

Use `jolt_value_t` for exact integers, booleans, Unicode, strings and collections.
The legacy float wrapper cannot preserve exact integer inputs or return
collections. Caller-owned input views are borrowed. String/collection output
views are VM-owned until the next successful typed run or VM destruction; copy
them if they must outlive that point. Failure preserves previously published
output. Use separate VMs or serialized access for mutable VM state.

`jolt_run_options_t` has a size field and a step budget: zero selects 100,000,
maximum 10,000,000. Typed calls/syntax have a 128-level bound; collections hold
up to 4,096 items, nesting up to 64, under a 16 MiB invocation arena. Image and
audio runners have their own scratch/execution contracts. Output is published
only after success, including supported in-place execution.

The runtime tools are `joltc`, `jolti`, `joltfmt`, `joltdoc` and
`joltscript-lsp`; SDK header installation does not build these executables.
Canonical references are `joltscript/docs/language.md`, `stdlib.md`, `tools.md`,
`examples.md`, `JOLTSCRIPT_PLAN.md`, and the installed API header comments.
