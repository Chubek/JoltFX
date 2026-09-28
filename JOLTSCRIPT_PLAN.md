# Joltscript Development Plan

## Current State

Joltscript currently has a minimal MVP implementation:

- **Glue Layer**: A bytecode compiler (`compiler.c`) that handles `(defkernel name [inputs...] expression)` with basic arithmetic operations (+, -, *, /, min, max, abs, floor, pow, sqrt, <, select)
- **Execution Layer**: A stack-based VM (`vm.c`) for JBC1 bytecode, pipeline execution with DAG resolution
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

### Phase 1: Expand the Compiler (Core Language Features)

**Goal**: Support the core language features from the spec.

**Tasks**:
1. Add special forms: `let`, `var`, `if`, `cond`, `when`, `unless`, `for`, `while`, `loop`, `fn`, `defn`, `do`
2. Add more types: `i32`, `i64`, `u8`, `u16`, `u32`, `u64`, `f32`, `f64`, `bool`, `char`, `string`
3. Add more operations: comparison (`<`, `>`, `<=`, `>=`, `=`, `!=`), logical (`and`, `or`, `not`), bitwise (`bitwise-and`, `bitwise-or`, `bitwise-xor`, `shl`, `shr`)
4. Add better error messages with source locations
5. Update Zoltan's `bytecode.rs` to match (byte-identical output required)

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
1. Create `joltscript/stdlib/` directory
2. Add `math.jolt` - Trigonometry, linear algebra, interpolation, random
3. Add `graphics.jolt` - Color spaces, transforms, filters, blend modes
4. Add `geometry.jolt` - Points, vectors, matrices, quaternions, splines
5. Add `time.jolt` - Curves, easing functions, keyframe interpolation
6. Add `collections.jolt` - Lists, vectors, maps, sets, sequences
7. Add `strings.jolt` - Manipulation, parsing, formatting, regex

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
1. Create `joltscript/tools/joltc/` - Compiler driver
2. Create `joltscript/tools/jolti/` - REPL
3. Create `joltscript/tools/joltfmt/` - Formatter
4. Create `joltscript/tools/joltdoc/` - Documentation generator
5. Create `joltscript/tools/joltscript-lsp/` - LSP server

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
- [ ] All tools are implemented and working
- [ ] All tests pass
- [ ] Documentation is complete
- [ ] Zoltan bytecode parity is maintained
