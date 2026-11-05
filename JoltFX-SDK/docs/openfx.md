# OpenFX module inspection (SDK 1.2)

Link `JoltFXSDK::openfx` and include `<joltfx/sdk/openfx.h>`. The implementation
uses the canonical headers from `third_party/openfx`; no engine or OpenFX C++
support library is required. Those headers are installed under `include/openfx`
and are also available with upstream spelling (`<ofxImageEffect.h>`) when linked
to the target. The public JoltFX inspection ABI contains no vendor types.

## Lifecycle

1. Supply `jfx_ofx_services_t` with your allocator/release callbacks. They must
   honor the requested alignment and their context must outlive the host.
2. Call `jfx_ofx_host_open` on the actual platform binary inside an `.ofx.bundle`,
   or on an unbundled native module. Bundle discovery is not performed.
3. Enumerate `jfx_ofx_host_plugin_count`; initialize `jfx_ofx_plugin_info_t.size`
   and call `jfx_ofx_host_plugin_info` for each index.
4. Close the host. Metadata is copied and remains valid after unloading.

Open requires `OfxGetNumberOfPlugins`, `OfxGetPlugin`, and each plugin's `setHost`
and `mainEntry`. The optional module-level `OfxSetHost` is called before
enumeration. Only image-effect API version 1 is accepted. `Load` and `Describe`
are lazy and cached. Failed descriptions unload successfully loaded plugins,
discard partial properties and permit retry; caller output stays unchanged.
Close sends `Unload` in reverse order before releasing the native module.

Use one serialized owner thread per handle. Callback reentry and duplicate opens
of the same loaded binary return `BUSY`, including alternate paths resolved to
the same native module handle. Native module open/close uses a nonblocking
process-local registry; contention also returns `BUSY`. The optional log callback
receives borrowed formatted messages. `jfx_ofx_host_error` exposes the latest
message/action failure; open failures are reported by the returned status.

## Inspection profile

Provided suites: Property V1 (typed scalar/vector storage, dimensions and reset),
Memory V1 (caller-owned allocator), Message V1 (questions answered No), and
ImageEffect V1 descriptor `getPropertySet`. Other image operations return
unsupported; unknown suites/versions return NULL. Plugins requiring parameter,
thread, interact or other suites even during Load/Describe may fail inspection
with `NOT_IMPLEMENTED` or a plugin error. This is not a general OpenFX host.

Bounds: 32 plugins per module, 128 properties per set, 16 values per property,
127-byte property names and 511-byte strings. Limits reject oversized inputs;
metadata label/grouping copies are truncated to 95 bytes. Host-seeded properties
reset to their initial values; dynamically created properties reset to empty.
Plugin identifiers must fit the 95-byte public field. Plugins must release
their suite allocations before unload. Native modules must obey the OpenFX C ABI
and must not throw exceptions across it. They execute trusted native code.

`supports_filter` means the descriptor advertises filter context and byte depth;
it does not establish clip/parameter compatibility. **Rendering is not implemented.**
The proposed execution API is retained in the uninstalled
`openfx/design/effect-host-draft.h`. Next steps are DescribeInContext, parameters,
instances, RGBA8 clips and action sequencing. Native loading on WASM returns
`NOT_IMPLEMENTED`.

## Verification

```sh
cmake -S JoltFX-SDK -B build/sdk-tests -DJOLTFX_SDK_BUILD_TESTS=ON
cmake --build build/sdk-tests
ctest --test-dir build/sdk-tests --output-on-failure
```

The fixture covers optional module-level setHost, cached metadata, typed property
round trips, memory ownership, unsupported features, callback reentry, duplicate
opens, failure-atomic output, and allocation-failure rollback/retry. The repository
SDK installation test verifies both build-tree and relocated C11/C++17 consumers.
