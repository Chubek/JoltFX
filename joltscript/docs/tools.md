# Joltscript Tools

## Overview

Joltscript ships with a set of tools for kernel development: compiler, REPL, formatter, documentation generator, and LSP server.

The compiler/evaluator implement the Phase 1–2 language and standard library.
The formatter, documentation generator and LSP are preliminary tools; their
full semantic integration is the next language-plan phase. Use `joltc --check`
or `zoltan verify` for compiler-backed validation.

## joltc — Compiler

Compiles `.jolt` source files to JBC1 bytecode.

### Usage

```bash
joltc [options] <input.jolt> [-o <output.jbc>]
```

### Options

| Option | Description |
|--------|-------------|
| `-o, --output <path>` | Write bytecode to `<path>` |
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `--check` | Validate only, don't emit bytecode |
| `--dump` | Dump disassembly to stdout |
| `--image-library <path>` | Validate the separate CPU image profile with prepended helpers; requires `--check`, emits no bytecode |

### Examples

```bash
# Compile a kernel
joltc kernel.jolt -o kernel.jbc

# Validate without emitting
joltc --check kernel.jolt

# Dump disassembly
joltc --dump kernel.jolt
```

## jolti — REPL

The current evaluator accepts complete kernels, with inputs initialized to zero.
For example, `jolti -e '(defkernel k [] (if 1 7 (/ 1 0)))'` returns `7`.

Typed kernels print exact integer, string, character, bool and floating results:
`jolti -e '(defkernel k [] 18446744073709551615u64)'` prints all 20 digits.
`joltc --dump` displays version-2 tree tags, links, tokens and source positions;
`joltfx info FILE.jbc` reports the typed container and node count.

Phase 2 adds compiled built-in imports and recursive collection output in
`jolti`. For example:

```sh
jolti -e '(import jolt.collections) (defkernel k [] (map + [1 2] [3 4]))'
# [4 6]
joltc joltscript/stdlib/math.jolt --check
```

All six standard-library interfaces can be checked/compiled as zero-output
module artifacts. See [stdlib.md](stdlib.md) for the API, CPU execution limits
and Python/CMake/vendored-PCRE2 build requirements.

Interactive Read-Eval-Print Loop for Joltscript.

### Usage

```bash
jolti [options]
```

### Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `-e, --eval <source>` | Evaluate a complete kernel and exit; inputs are zero-initialized |

### Examples

```bash
# Start REPL
jolti

# Evaluate expression
jolti -e '(defkernel k [] (+ 1 2))'
```

### REPL Commands

- `quit` or `exit` — Exit the REPL
- `Ctrl+D` — Exit the REPL

Each evaluation is independent; imports and definitions belong in the same
source submission as the kernel. Module-only artifacts cannot be invoked.

## joltfmt — Formatter

Formats `.jolt` source files according to the Joltscript style guide.

### Usage

```bash
joltfmt [options] <input.jolt> [-o <output.jolt>]
```

### Options

| Option | Description |
|--------|-------------|
| `-o, --output <path>` | Write formatted output to `<path>` |
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `--check` | Check if file is formatted (exit 1 if not) |
| `--stdin` | Read from stdin, write to stdout |

### Examples

```bash
# Format a file
joltfmt kernel.jolt -o kernel-formatted.jolt

# Check if formatted
joltfmt --check kernel.jolt

# Format from stdin
cat kernel.jolt | joltfmt --stdin
```

### Current formatting scope

The standalone token formatter is not a compiler-backed typed-language formatter.
It currently drops semicolon comments, including metadata, and does not implement
the planned import sorting, binding alignment or width rules. Preserve original
sources and metadata when using it; validate formatted source with `joltc`.

## joltdoc — Documentation Generator

Generates Markdown or HTML documentation from `.jolt` source files.

It extracts source forms/comments without the shared semantic checker. The
hand-maintained [library reference](stdlib.md) and checked `.jolt` interfaces
are authoritative for the Phase 2 API.

### Usage

```bash
joltdoc [options] <input.jolt> [-o <output.md>]
```

### Options

| Option | Description |
|--------|-------------|
| `-o, --output <path>` | Write documentation to `<path>` |
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `--html` | Generate HTML instead of Markdown |
| `--stdin` | Read from stdin, write to stdout |

### Examples

```bash
# Generate Markdown documentation
joltdoc kernel.jolt -o kernel.md

# Generate HTML documentation
joltdoc --html kernel.jolt -o kernel.html

# Generate from stdin
cat kernel.jolt | joltdoc --stdin
```

## joltscript-lsp — LSP Server

Language Server Protocol server for Joltscript.

### Usage

```bash
joltscript-lsp [options]
```

### Options

| Option | Description |
|--------|-------------|
| `-h, --help` | Show help message |
| `-v, --version` | Show version information |
| `--stdio` | Use stdio for communication (default) |
| `--tcp <port>` | Parsed but not implemented; the server still uses stdio |

### Current protocol scope

The server implements initialization and basic stdio request handling. Hover
currently returns null, completion returns an empty list, and document-open
publishes an empty diagnostic list; these are not compiler-backed language
services. Phase 1–2 compiler/stdlib completion does not imply completed LSP
features. TCP mode is accepted as an option but is not implemented.

### Examples

```bash
# Start LSP server with stdio
joltscript-lsp --stdio
```

## Building from Source

```bash
# Configure
cmake --preset default

# Build
cmake --build build -j$(nproc)

# Run tests
ctest --test-dir build -R joltscript
```

## Installation

```bash
# Install to system
cmake --install build

# Or specify prefix
cmake --install build --prefix /usr/local
```

## Environment Variables

| Variable | Description |
|----------|-------------|
| `JOLTFX_HOME` | JoltFX installation directory |
| `PATH` | Must include JoltFX `bin/` directory |

## Troubleshooting

### joltc fails to compile

- Check syntax with `joltc --check`
- Verify all bindings are defined
- Check for duplicate bindings

### jolti shows no output

- Verify input is valid Joltscript
- Check for syntax errors
- Try `jolti -e '(defkernel k [] (+ 1 2))'` to test

### joltfmt changes nothing

- File may already be formatted
- Check with `joltfmt --check`

### joltdoc generates empty output

- Verify source contains documentation comments
- Check for `defkernel`, `defn`, `defmacro` forms
