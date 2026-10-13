# Joltscript Development Plan

## Current State

Joltscript implements bounded f32 and typed JBC1 language profiles:

- **Glue Layer**: A bytecode compiler (`compiler.c`) with arithmetic, comparison,
  logical and bitwise operations, lexical bindings/mutation, sequences and lazy
  conditionals. The CPU image interpreter is a separate bounded profile.
- **Execution Layer**: A stack-based VM (`vm.c`) for legacy bytecode and a typed
  tree VM with functions, loops, exact values, collections and the six built-in
  standard-library modules; pipeline execution with DAG resolution
- **Zoltan**: A second implementation of the JBC1 emitter in Rust (must be byte-identical to C)

## Target State (from AGENTS.md)

The full Joltscript language includes:

- **Compiler frontend**: Parser, typechecker, AST
- **IR generation**: SSA-based, kernel-aware
- **Backends**: C, Rust, Python, Go
- **Runtime**: ARC, arena, intrinsics
- **Standard library**: math, graphics, geometry, time, io, collections, strings
- **Tools**: joltc, jolti, joltfmt, joltdoc, joltscript-lsp
- **Tests**: unit, integration, conformance
- **Benchmarks**

## Development Plan

### Implementation checkpoint (2026-10-13)

Phase 1 core-language tasks are implemented. Legacy f32 programs retain JBC1
version 1. Execution ABI 0.8 adds version-2 typechecked tree bytecode for all
listed primitive types, annotations/casts, `for`/`while`/`loop`, `break`/`continue`,
named functions, recursive calls and lexical closures. Execution is step-,
depth- and environment-budgeted; exact values use the new typed C API.

C and Rust independently read/serialize version 2, with a shared portable C
typechecker/VM used by Zoltan's safe Rust bridge. Native preview and CPU backend
execution support it; Vulkan routes version 2 through CPU fallback. Existing
HTML/WAT lowering remains version-1-only and explicitly rejects version 2.
Typed shader/export lowering remains additional backend work. The CPU image
interpreter remains a distinct profile.

Phase 2 is implemented with 262 intrinsic exports across math, graphics,
geometry, time, collections and strings. The six `.jolt` interfaces generate an
embedded catalogue; imports, immutable aggregate values, higher-order functions,
bounded sequence generators and PCRE2 strings execute through the typed VM.
Execution ABI 0.9 adds collection views while preserving the value struct layout.
All exported intrinsics have execution/C–Rust parity coverage. See
`joltscript/docs/stdlib.md` for exact semantics, budgets and build dependencies.

Phase 3 tools are implemented. See `joltscript/docs/language.md` for the
versioned language and backend boundaries, and `joltscript/docs/tools.md` for
the compiler-backed documentation/LSP behavior.

### Phase 1: Expand the Compiler (Core Language Features)

**Goal**: Support the core language features from the spec.

**Tasks**:
1. [x] Add special forms: `let`, `var`, `if`, `cond`, `when`, `unless`, `for`, `while`, `loop`, `fn`, `defn`, `do`
2. [x] Add more types: `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `char`, `string`
3. [x] Add more operations: comparison (`<`, `>`, `<=`, `>=`, `=`, `!=`), logical (`and`, `or`, `not`), bitwise (`bitwise-and`, `bitwise-or`, `bitwise-xor`, `shl`, `shr`)
4. [x] Add better error messages with source locations
5. [x] Update Zoltan's `bytecode.rs` to match (byte-identical output required)

**Files to modify**:
- `joltscript/layers/glue/src/compiler.c` - Expand the compiler
- `joltscript/layers/glue/include/joltscript/compiler.h` - Update the API
- `joltscript/layers/execution/include/joltscript/vm.h` - Add new opcodes
- `joltscript/layers/execution/src/vm.c` - Implement new opcodes
- `joltscript/layers/execution/src/bytecode.c` - Update validation
- `zoltan/src/bytecode.rs` - Match the C implementation

### Phase 2: Add Standard Library

**Goal**: Provide a standard library covering common operations.

**Tasks**:
1. [x] Create `joltscript/stdlib/` directory
2. [x] Add `math.jolt` - Trigonometry, linear algebra, interpolation, random
3. [x] Add `graphics.jolt` - Color spaces, transforms, filters, blend modes
4. [x] Add `geometry.jolt` - Points, vectors, matrices, quaternions, splines
5. [x] Add `time.jolt` - Curves, easing functions, keyframe interpolation
6. [x] Add `collections.jolt` - Lists, vectors, maps, sets, sequences
7. [x] Add `strings.jolt` - Manipulation, parsing, formatting, regex

**Files to create**:
- `joltscript/stdlib/math.jolt`
- `joltscript/stdlib/graphics.jolt`
- `joltscript/stdlib/geometry.jolt`
- `joltscript/stdlib/time.jolt`
- `joltscript/stdlib/collections.jolt`
- `joltscript/stdlib/strings.jolt`

### Phase 3: Add Tools

**Goal**: Provide the tools described in the spec.

**Tasks**:
1. [x] Create `joltscript/tools/joltc/` - Compiler driver
2. [x] Create `joltscript/tools/jolti/` - REPL
3. [x] Create `joltscript/tools/joltfmt/` - Formatter
4. [x] Create `joltscript/tools/joltdoc/` - Documentation generator
5. [x] Create `joltscript/tools/joltscript-lsp/` - LSP server

**Files to create**:
- `joltscript/tools/joltc/src/main.c`
- `joltscript/tools/jolti/src/main.c`
- `joltscript/tools/joltfmt/src/main.c`
- `joltscript/tools/joltdoc/src/main.c`
- `joltscript/tools/joltscript-lsp/src/main.c`

### Phase 4: Add Tests

**Goal**: Comprehensive test coverage.

**Tasks**:
1. Add unit tests for the compiler
2. Add unit tests for the VM
3. Add integration tests for kernel execution
4. Add conformance tests for cross-target consistency
5. Add benchmarks

**Files to create**:
- `joltscript/tests/unit/test_compiler.c`
- `joltscript/tests/unit/test_vm.c`
- `joltscript/tests/integration/test_kernels.c`
- `joltscript/tests/conformance/test_conformance.c`
- `joltscript/tests/benchmarks/bench_compiler.c`

### Phase 5: Add Documentation

**Goal**: Comprehensive documentation.

**Tasks**:
1. Create `joltscript/docs/` directory
2. Add language reference
3. Add stdlib documentation
4. Add tool documentation
5. Add examples

**Files to create**:
- `joltscript/docs/language.md`
- `joltscript/docs/stdlib.md`
- `joltscript/docs/tools.md`
- `joltscript/docs/examples.md`

## Implementation Order

1. **Phase 1**: Expand the compiler (core language features)
2. **Phase 2**: Add standard library
3. **Phase 3**: Add tools
4. **Phase 4**: Add tests
5. **Phase 5**: Add documentation

## Success Criteria

- [ ] Compiler supports all special forms from the spec
- [ ] Compiler supports all types from the spec
- [ ] Compiler supports all operations from the spec
- [ ] Standard library covers all modules from the spec
- [x] All tools are implemented and working
- [ ] All tests pass
- [ ] Documentation is complete
- [ ] Zoltan bytecode parity is maintained
