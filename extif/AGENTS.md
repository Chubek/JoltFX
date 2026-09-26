```markdown
# AGENTS.md — Extensions

## Overview

JoltFX supports scripting extensions through embedded language runtimes. This
guide covers contributing bindings for Lua, mruby, JavaScript (QuickJS), and
Python (optionally), as well as the FFI bridge layer that exposes the engine's
C API to each runtime.

---

## Repository Layout

```
extensions/
  common/
    ffi_bridge.h         # abstract FFI interface all extensions implement
    type_registry.h      # engine type → script type mapping
    callback_registry.h  # script → engine callback registration
    error_bridge.h       # error propagation between C and script
  lua/
    src/
      lua_init.c         # Lua state initialization and sandbox setup
      lua_bindings.c     # manual bindings for core engine types
      lua_kernel.c       # kernel registration and dispatch
      lua_buffer.c       # buffer/texture wrapping
      lua_event.c        # event bus subscription from Lua
    include/
      jfx_lua.h          # public Lua extension API
    scripts/
      stdlib.lua         # standard library loaded into every Lua kernel
    tests/
      test_kernel.lua
      test_buffer.lua
  mruby/
    src/
      mruby_init.c       # mruby state and GC config
      mruby_bindings.c   # manual bindings
      mruby_kernel.c
      mruby_buffer.c
    include/
      jfx_mruby.h
    scripts/
      stdlib.rb
    tests/
      test_kernel.rb
  quickjs/
    src/
      qjs_init.c         # QuickJS runtime and context
      qjs_bindings.c     # manual bindings
      qjs_kernel.c
      qjs_buffer.c
    include/
      jfx_qjs.h
    scripts/
      stdlib.js
    tests/
      test_kernel.js
  python/                # optional, heavier runtime
    src/
      py_init.c
      py_bindings.c
      py_kernel.c
    include/
      jfx_py.h
    scripts/
      stdlib.py
    tests/
      test_kernel.py
  tests/
    conformance/         # cross-language conformance tests
    perf/                # script overhead benchmarks

---

## FFI Bridge Contract

Every extension runtime implements the interface in `common/ffi_bridge.h`:

c
typedef struct jfx_script_runtime_t {
    /* Lifecycle */
    jfx_result_t (*init)(const jfx_script_desc_t *desc, jfx_script_runtime_t **out);
    void         (*shutdown)(jfx_script_runtime_t *rt);

    /* Script execution */
    jfx_result_t (*load_script)(jfx_script_runtime_t *, const char *source, const char *name);
    jfx_result_t (*call_function)(jfx_script_runtime_t *, const char *name,
                                  const jfx_value_t *args, size_t argc, jfx_value_t *ret);

    /* Kernel interface */
    jfx_result_t (*register_kernel)(jfx_script_runtime_t *, const char *name, jfx_kernel_fn fn);
    jfx_result_t (*invoke_kernel)(jfx_script_runtime_t *, const char *name,
                                  const jfx_kernel_params_t *params, jfx_buffer_t *out);

    /* Type marshalling */
    jfx_result_t (*to_native)(jfx_script_runtime_t *, jfx_script_value_t script_val, jfx_value_t *out);
    jfx_result_t (*from_native)(jfx_script_runtime_t *, const jfx_value_t *native_val, jfx_script_value_t *out);

    /* Callbacks */
    jfx_result_t (*register_callback)(jfx_script_runtime_t *, const char *event_name, jfx_script_value_t fn);

    /* GC control */
    void (*gc_collect)(jfx_script_runtime_t *);
    void (*gc_pause)(jfx_script_runtime_t *);
    void (*gc_resume)(jfx_script_runtime_t *);
} jfx_script_runtime_t;

All bindings must route through this interface. Engine code never calls Lua,
mruby, or QuickJS APIs directly.

---

## Ownership Rules

| Area                          | Gate before merge                         |
|-------------------------------|-------------------------------------------|
| `common/ffi_bridge.h`         | Extensions lead + architecture review     |
| `lua/`                        | Lua owner + one reviewer                  |
| `mruby/`                      | mruby owner + one reviewer                |
| `quickjs/`                    | QuickJS owner + one reviewer              |
| `python/`                     | Python owner + two reviewers              |
| `common/type_registry.h`      | All active extension owners sign off      |
| Conformance tests             | QA sign-off                               |

Changes to `ffi_bridge.h` affect every extension. Coordinate across all owners
before modifying it.

---

## Build and Toolchain

Extension builds are optional; each is gated by a CMake flag:

sh
# Lua (default ON, ~250 KB runtime)
cmake -DJFX_EXT_LUA=ON ..
cmake --build . --target jfx_lua

# mruby (default ON, ~400 KB runtime)
cmake -DJFX_EXT_MRUBY=ON ..
cmake --build . --target jfx_mruby

# QuickJS (default ON, ~600 KB runtime)
cmake -DJFX_EXT_QUICKJS=ON ..
cmake --build . --target jfx_quickjs

# Python (default OFF, ~15 MB runtime; desktop only)
cmake -DJFX_EXT_PYTHON=ON ..
cmake --build . --target jfx_python

Run conformance tests before submitting any extension change:

sh
ctest --test-dir build -R ext_conformance -V

All enabled extensions must pass conformance. A change that breaks conformance
in any extension is blocked.

---

## Memory Management

Extension runtimes allocate through the engine's memory budget, not the system
allocator. Lua, mruby, and QuickJS all support custom allocators; configure
them to use Tilly's arena or pool allocators during initialization.

c
/* Example: Lua allocator hookup */
static void *lua_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
    jfx_allocator_t *alloc = (jfx_allocator_t *)ud;
    if (nsize == 0) {
        jfx_free(alloc, ptr);
        return NULL;
    }
    return jfx_realloc(alloc, ptr, nsize);
}

lua_State *L = lua_newstate(lua_alloc, engine_allocator);

Never call `malloc` or `free` directly in extension code. Do not leak script
objects across frames; tie their lifetimes to the engine's frame graph or to
explicit user-managed pools.

---

## Sandboxing

All script runtimes must run in a restricted sandbox by default. Remove or stub
out dangerous standard library functions before executing user scripts.

### Lua Sandbox

Remove or stub:
- `os.execute`, `os.exit`, `os.remove`, `os.rename`, `os.tmpname`
- `io.open`, `io.popen`, `io.lines`, `io.input`, `io.output`
- `loadfile`, `dofile`
- `package.loadlib`, `package.cpath`
- `debug.debug`, `debug.getinfo` (retain `debug.traceback` for error reporting)

Allow:
- `math.*`, `string.*`, `table.*`
- `coroutine.*` (for user-managed cooperative multitasking)
- Custom `jfx.*` namespace for engine bindings

### mruby Sandbox

Compile with `MRB_DISABLE_STDIO` and remove:
- `Kernel#system`, `Kernel#exec`, `Kernel#spawn`
- `File`, `Dir`, `IO` classes
- `eval` unless explicitly re-enabled for trusted scripts

### QuickJS Sandbox

Do not expose:
- `std.open`, `std.popen`, `std.remove`
- `os.exec`, `os.system`
- `globalThis.fetch` (no network I/O from scripts by default)

Provide a safe `jfx` global with engine bindings only.

---

## Type Marshalling

The type registry in `common/type_registry.h` defines bidirectional mappings
between engine types and script types.

c
typedef enum jfx_value_type_t {
    JFX_TYPE_NIL,
    JFX_TYPE_BOOL,
    JFX_TYPE_INT,
    JFX_TYPE_FLOAT,
    JFX_TYPE_VEC2,
    JFX_TYPE_VEC3,
    JFX_TYPE_VEC4,
    JFX_TYPE_COLOR,
    JFX_TYPE_BUFFER,
    JFX_TYPE_TEXTURE,
    JFX_TYPE_STRING,
    JFX_TYPE_USERDATA,
} jfx_value_type_t;

typedef struct jfx_value_t {
    jfx_value_type_t type;
    union {
        bool b;
        int64_t i;
        double f;
        float vec2[2];
        float vec3[3];
        float vec4[4];
        uint32_t color; // RGBA8
        jfx_buffer_t *buffer;
        jfx_texture_t *texture;
        const char *str;
        void *userdata;
    };
} jfx_value_t;

Each extension implements `to_native` and `from_native` to convert between
`jfx_value_t` and the script runtime's native representation (Lua stack,
mruby `mrb_value`, QuickJS `JSValue`, Python `PyObject *`).

Type mismatches must be caught and reported as `JFX_RESULT_TYPE_ERROR` before
propagating upward. Never allow unchecked type coercion (e.g., silently
converting a string to zero).

---

## Kernel Registration

Kernels written in a script language are registered with the engine at load
time. The engine stores a reference to the script function and invokes it via
`invoke_kernel` when the kernel is scheduled.

### Lua Example

lua
-- user_kernels/blur.lua
function blur(input, output, params)
    local radius = params.radius or 5.0
    local width, height = input:dimensions()
    for y = 0, height - 1 do
        for x = 0, width - 1 do
            local sum = {0, 0, 0, 0}
            local count = 0
            for dy = -radius, radius do
                for dx = -radius, radius do
                    local px = input:sample(x + dx, y + dy)
                    sum[1] = sum[1] + px[1]
                    sum[2] = sum[2] + px[2]
                    sum[3] = sum[3] + px[3]
                    sum[4] = sum[4] + px[4]
                    count = count + 1
                end
            end
            output:write(x, y, {
                sum[1] / count,
                sum[2] / count,
                sum[3] / count,
                sum[4] / count
            })
        end
    end
end

jfx.register_kernel("blur", blur)

The binding layer wraps `input` and `output` as userdata with metatables that
expose `dimensions()`, `sample(x, y)`, and `write(x, y, color)`.

---

## Buffer and Texture Wrapping

Buffers and textures passed to scripts are wrapped in lightweight userdata
objects that hold a pointer to the native `jfx_buffer_t` or `jfx_texture_t`.
The userdata must not outlive the native object; tie its lifetime to the
kernel invocation scope.

Never copy the entire buffer or texture into the script runtime. Provide
accessor methods that read and write through the native pointer.

### Lua Buffer Wrapper

c
typedef struct {
    jfx_buffer_t *buffer;
    size_t width;
    size_t height;
    size_t channels;
} lua_buffer_t;

static int lua_buffer_sample(lua_State *L) {
    lua_buffer_t *buf = luaL_checkudata(L, 1, "jfx.Buffer");
    int x = luaL_checkinteger(L, 2);
    int y = luaL_checkinteger(L, 3);
    if (x < 0 || x >= buf->width || y < 0 || y >= buf->height) {
        return luaL_error(L, "sample out of bounds");
    }
    float *data = jfx_buffer_map(buf->buffer);
    size_t idx = (y * buf->width + x) * buf->channels;
    lua_createtable(L, buf->channels, 0);
    for (size_t i = 0; i < buf->channels; ++i) {
        lua_pushnumber(L, data[idx + i]);
        lua_rawseti(L, -2, i + 1);
    }
    jfx_buffer_unmap(buf->buffer);
    return 1;
}

Map the buffer once per batch of accesses, not once per pixel. Provide
`begin_access()` and `end_access()` methods if the script needs to iterate
over the entire buffer.

---

## Event Bus Integration

Scripts can subscribe to engine events (frame start, resource load, user input)
through the callback registry in `common/callback_registry.h`.

lua
-- Lua example
jfx.on("frame_start", function(frame_number)
    print("Frame " .. frame_number)
end)

jfx.on("key_press", function(key, mods)
    if key == "escape" then
        jfx.quit()
    end
end)

The binding layer registers a native C callback with the engine's event bus;
when the event fires, the native callback invokes the script function via
`call_function`.

Script callbacks must be removed when the script is unloaded. Never leak a
callback registration past the lifetime of the script runtime.

---

## Error Propagation

Errors in script code must propagate upward as `jfx_result_t` and push a
context string onto the error stack defined in TillyZ. Capture the script
stack trace and include it in the error context.

c
/* Example: Lua error capture */
static jfx_result_t lua_safe_call(lua_State *L, int nargs, int nret) {
    int base = lua_gettop(L) - nargs;
    lua_pushcfunction(L, lua_error_handler);
    lua_insert(L, base);
    int status = lua_pcall(L, nargs, nret, base);
    lua_remove(L, base);
    if (status != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        jfx_error_push("Lua error: %s", msg);
        lua_pop(L, 1);
        return JFX_RESULT_SCRIPT_ERROR;
    }
    return JFX_RESULT_OK;
}

Do not silently swallow script errors. Every error must be logged and returned
to the caller.

---

## Garbage Collection

Lua, mruby, and QuickJS use automatic garbage collection. The engine controls
when GC runs to avoid pauses during frame rendering.

- Call `gc_pause()` at the start of the frame.
- Call `gc_resume()` after presenting.
- Call `gc_collect()` during idle periods or between scenes.

Never trigger a full GC collection during a frame render. Allow incremental
GC to run between frames if the runtime supports it (Lua 5.4+ incremental GC,
mruby incremental GC).

For Python, reference counting is immediate; call `Py_DECREF` as soon as a
reference is no longer needed. Do not rely on the cyclic GC; structure Python
bindings to avoid cycles.

---

## Performance Guidelines

- Cache script function references across invocations; do not look up the
  function by name on every kernel dispatch.
- Minimize boundary crossings. If a kernel runs once per pixel, it will be
  slow; batch operations where possible or compile the kernel to native code.
- Provide a JIT compilation path for hot kernels. LuaJIT, mruby JIT (optional),
  and QuickJS bytecode caching can significantly reduce overhead.
- Profile script overhead separately from native code. Use `ctest -R ext_perf`
  and post results in the PR for any hot-path change.
- For Python, use `PyPy` instead of CPython if available; the JIT makes a
  10× difference for compute-heavy kernels.

---

## Conformance Tests

Conformance tests in `tests/conformance/` run the same logic in every
supported extension language. They cover:

- Kernel registration and invocation
- Buffer create, read, write, bounds checking
- Texture sampling and writing
- Type marshalling (all `jfx_value_t` types)
- Error propagation (script error → engine error stack)
- Callback registration and unregistration
- GC integration (no leaks, no use-after-free)
- Sandbox enforcement (blocked I/O operations return errors)

Add or extend a conformance test when fixing a bug that affects the script
interface.

---

## Adding a New Extension Language

1. Create a directory `extensions/<lang>/` with `src/`, `include/`, `scripts/`,
   and `tests/`.
2. Implement every function pointer in `jfx_script_runtime_t`.
3. Add a `CMakeLists.txt` gated by `JFX_EXT_<LANG>`.
4. Configure the runtime's allocator to use Tilly's memory system.
5. Implement sandbox restrictions appropriate for the language.
6. Add type marshalling for all `jfx_value_t` types.
7. Port the full conformance test suite; all tests must pass.
8. Add a CI lane for the new extension.
9. Document runtime size, initialization cost, and any platform limitations in
   `extensions/<lang>/README.md`.
10. Open a PR with architecture review gate satisfied before creating the branch.

---

## Security Considerations

- User scripts are untrusted. Assume every script is malicious until proven
  otherwise.
- Never expose raw pointers or memory addresses to scripts.
- Validate all array indices and buffer offsets before dereferencing.
- Limit script execution time; provide a timeout or instruction count budget.
- Disable `eval`, `load`, or equivalent dynamic code execution unless the user
  explicitly enables it for trusted scripts.
- Do not serialize script bytecode from untrusted sources without signature
  verification; bytecode can encode arbitrary memory writes on some runtimes.

---

## PR Checklist

- [ ] Conformance tests pass for all enabled extensions
- [ ] No memory leaks detected by Valgrind or ASan
- [ ] Sandbox restrictions verified (blocked I/O returns errors)
- [ ] Type marshalling covers all `jfx_value_t` types
- [ ] Errors propagate to the engine error stack with script stack traces
- [ ] GC hooks integrated (`gc_pause`, `gc_resume`, `gc_collect`)
- [ ] Performance overhead measured and posted
- [ ] Gate requirement met (see Ownership Rules)
- [ ] `extensions/<lang>/README.md` updated for new language or interface change

---

## Common Mistakes

**Leaking script objects.** Every Lua reference, mruby `RObject`, QuickJS
`JSValue`, or Python `PyObject` must be released when no longer needed. Use
RAII wrappers or explicit reference tracking.

**Exposing unsafe APIs.** Leaving `os.execute` or `io.open` accessible in the
sandbox allows arbitrary code execution. Strip or stub all I/O and system APIs.

**Ignoring script errors.** A script error that is caught and ignored will
silently produce incorrect output. Always propagate errors upward and log them.

**Synchronous GC during render.** A full GC pause during frame rendering causes
visible hitches. Pause GC during the frame and resume it afterward.

**Type coercion without validation.** Converting a Lua `nil` to an integer
without checking produces zero, which may not be the user's intent. Fail
explicitly on type mismatches.

**Unbounded script execution.** A script with an infinite loop will hang the
engine. Impose an instruction count budget or wall-time limit on all script
invocations.

**Copying entire buffers.** Copying a 4K RGBA buffer into the script runtime
allocates 32 MB per kernel invocation. Provide accessor methods that read and
write through the native pointer.

---

## Contacts

- Lua extension: `#joltfx-ext-lua`
- mruby extension: `#joltfx-ext-mruby`
- QuickJS extension: `#joltfx-ext-quickjs`
- Python extension: `#joltfx-ext-python`
- FFI bridge design: `#joltfx-ext-core`
- Conformance tests: `#joltfx-qa`

For questions about the FFI bridge or adding a new extension language, post in
`#joltfx-ext-core`.
