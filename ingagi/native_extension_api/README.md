# Ingagi native-module ABI 1.0.0

`Ingagi-Module.h` is the canonical ABI contract. `joltfx_embed` implements it as
a Go `c-shared` library. C++, D, Rust, Python and Java declarations use the same
fixed-width values and native pointer/`size_t` layouts; they do not use a second
wire format. This ABI is separate from the Glue layer ABI and bytecode images.

## Lifecycle and ownership

1. Check `ingagi_abi_version() == INGAGI_ABI_VERSION`.
2. Create a handle with `ingagi_create`. A zero budget selects 1,000,000
   instructions per invocation; call depth is limited to 256.
3. Register native module descriptors, then load UTF-8 source.
4. Call a named function with scalar values.
5. Destroy the handle before unloading its callback code or freeing userdata.

Every handle owns a VM and private ECS world. Source loads preserve native
registrations and the world; a failed compile or initializer leaves the previous
VM installed. Initializer effects on callbacks/world are not transactional.
Script printing is discarded by this embedding surface.

Concurrent and reentrant operations on the same handle return `INGAGI_BUSY`.
Different handles may run independently. Do not destroy a handle or unload the
embedding library while another thread is using it. Keep the Go shared library
loaded for the process lifetime; closing a session releases its VM, not the Go
runtime. The Java FFM lookup uses a global arena for this reason. The higher-level wrappers
require deterministic `close`/RAII ownership; Python has no automatic finalizer.

Names and source are pointer/length byte spans, UTF-8 without embedded NUL.
Names are ASCII identifiers of at most 255 bytes. Source is limited to 4 MiB;
each call/module is limited to 256 arguments/functions. Native function names
must not collide with builtin or script names (the VM's normal lookup order
still applies). Registration is all-or-nothing and duplicate native names fail.

Descriptors and names are copied on registration. Function pointers and userdata
are borrowed until handle destruction. Callbacks must finish synchronously, not
retain argument/result/error pointers, and not unwind language exceptions across
the C boundary. Python converts callback exceptions into runtime diagnostics.
Rust callbacks must catch panics themselves; Java upcalls must catch exceptions.

The caller owns all storage. Diagnostics are optional (`NULL, 0`); otherwise the
library writes a truncated NUL-terminated message, at most 4096 bytes. Result
storage is meaningful only on success. Buffers must point to valid memory;
status checks cannot validate arbitrary foreign addresses.

## Values and errors

`ingagi_value` is 24 bytes: kind/reserved at offsets 0/4, signed integer at 8,
double at 16. Reserved is zero. Supported values are nil, boolean (integer 0/1),
signed int64 and float64. Strings, aggregates, handles and ECS data do not cross
this ABI yet; they yield `INGAGI_UNSUPPORTED` rather than implicit conversion.

Statuses: OK=0, INVALID_ARGUMENT=1, INVALID_HANDLE=2, COMPILE_ERROR=3,
RUNTIME_ERROR=4, UNSUPPORTED=5, BUSY=6, INTERNAL_ERROR=7. Host callback failures
surface as script RUNTIME_ERROR. Instruction budgets do not bound host callbacks,
memory or wall time; this API is not a sandbox.

## Build and use

From the repository root:

```sh
cmake -S ingagi -B /tmp/opencode/ingagi-embed -DINGAGI_GOROOT=/usr/lib/go
cmake --build /tmp/opencode/ingagi-embed --parallel 4
ctest --test-dir /tmp/opencode/ingagi-embed --output-on-failure
cmake --install /tmp/opencode/ingagi-embed --prefix /tmp/opencode/ingagi-install
```

Or enable `JFX_INGAGI=ON` in the root build. The standalone CMake build avoids
building unrelated engine libraries. It exports the CMake target `ingagi_embed`
for in-tree consumers, CLI `bin/ingagi`, library `lib/libingagi_embed.so` (platform
suffix as appropriate), headers and binding sources. It currently builds host
artifacts on Linux/macOS, not cross targets or Windows import libraries.
Go 1.21+, C and C++20 compilers are required.

Native scannerless recognition is opt-in with `INGAGI_NOVOPARSE=ON` and
`INGAGI_NOVOPARSE_LIBRARY_DIR` pointing to a built NovoParse runtime. Build that
runtime with `ingagi/scripts/test-native.sh`. Untagged builds use the handwritten
AST parser. No parser grammar was changed for this ABI.

Python installation names the file `ingagi_module.py`:

```python
from ingagi_module import Session
with Session('/tmp/opencode/ingagi-embed/lib/libingagi_embed.so') as vm:
    vm.register('double_it', 1, lambda n: n * 2)
    vm.load('fn main() { return double_it(21); }')
    assert vm.call('main') == 42
```

C++ includes `Ingagi-Module.hpp` and uses `ingagi::session`. Rust includes
`ingagi_module.rs` and links `ingagi_embed`; its owning Session is deliberately
not Send/Sync. D imports `ingagi_module.di`. Java requires Java 22+, a 64-bit host
and `--enable-native-access=ALL-UNNAMED`; its FFM API supports raw module/upcall
descriptors as well as scalar calls. The package-private `IngagiModule` source
may be placed in the host's package. CMake runs available language smoke tests;
absent optional compilers are not downloaded.

This is a functional VM embedding surface. Desktop game-engine tab wiring and
full engine API/resource marshalling are separate work.
