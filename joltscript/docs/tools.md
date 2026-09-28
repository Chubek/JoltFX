# Joltscript Tools

## Overview

Joltscript ships with a set of tools for kernel development: compiler, REPL, formatter, documentation generator, and LSP server.

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
| `-e, --eval <expr>` | Evaluate expression and exit |

### Examples

```bash
# Start REPL
jolti

# Evaluate expression
jolti -e '(+ 1 2)'
```

### REPL Commands

- `quit` or `exit` — Exit the REPL
- `Ctrl+D` — Exit the REPL

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

### Style Rules

- 2-space indentation
- Align `let`/`var` bindings vertically when > 2
- Trailing commas in multi-line collections
- Max line width: 100 columns
- Sort imports alphabetically by module path

## joltdoc — Documentation Generator

Generates Markdown or HTML documentation from `.jolt` source files.

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
| `--tcp <port>` | Use TCP for communication |

### Features

- **Hover** — Show documentation on hover
- **Completion** — Code completion
- **Diagnostics** — Error reporting

### Examples

```bash
# Start LSP server with stdio
joltscript-lsp --stdio

# Start LSP server with TCP
joltscript-lsp --tcp 7072
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
- Try `jolti -e '(+ 1 2)'` to test

### joltfmt changes nothing

- File may already be formatted
- Check with `joltfmt --check`

### joltdoc generates empty output

- Verify source contains documentation comments
- Check for `defkernel`, `defn`, `defmacro` forms
