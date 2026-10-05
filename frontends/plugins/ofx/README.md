# OpenFX host adapter

Implements the **host** side of the OpenFX 1.5 image-effect API: it discovers
OFX bundles, drives the plugin action lifecycle, provides the property,
parameter and image-effect suites, and hands plugin-rendered frames back as
plain float RGBA.

Unlike the After Effects / Premiere / DaVinci bridges elsewhere in
`frontends/plugins/`, this needs **no proprietary host SDK**. It implements the
OFX suites itself and links only the engine's C API.

- Public header: `include/jfx/jfx_ofx.h` (`jfx_` prefix, size-guarded structs,
  `out_*` outputs last, per `src/AGENTS.md`).
- Target: `jfx_ofx_host`, gated by `JFX_PLUGIN_OPENFX` (default `ON`).
- OFX headers: `third_party/openfx` (submodule, OFX 1.5.1), included **private**
  so they never leak into a consumer's include path.
- Test: `ofx_host` (`tests/unit/frontends/test_ofx_host.c`).

## Building

The submodule must be present:

```bash
git submodule update --init third_party/openfx
cmake -DJFX_PLUGIN_OPENFX=ON ..
cmake --build build --target jfx_ofx_host
```

Configuring with `JFX_PLUGIN_OPENFX=ON` and no submodule fails with an explicit
message rather than a confusing missing-header error.

## Scope: what this host does and does not provide

This is a **CPU, single-image, non-animated, non-tiled** host. The
`OfxHost::fetchSuite` callback deliberately returns `NULL` for every suite it
does not implement, which is how OFX tells a plugin what is unavailable:

| Provided | Not provided |
|---|---|
| `OfxPropertySuite` | `OfxTimeSuite` |
| `OfxParameterSuite` | `OfxInteractSuite` |
| `OfxImageEffectSuite` | `OfxMessageSuite`, `OfxProgressSuite` |
| | `OfxMemorySuite`, `OfxMultiThreadSuite` |
| | `OfxParametricParameterSuite` |
| | `OfxOpenGLRenderSuite` / GPU suites |

Host capabilities advertised to plugins, in `fill_host_properties()`:

- float RGBA only
- `SupportsMultiResolution = 0`, `SupportsTiles = 0`
- `TemporalClipAccess = 0`, `SupportsOverlays = 0`
- no parameter animation (`SupportsCustomAnimation`, and the string, boolean and
  choice variants, are all `0`)

Declaring these up front is deliberate: a plugin that knows the host is
non-tiled and non-animated will not ask for services that would fail later at
render time.

Contexts: all six `jfx_ofx_context_t` values are accepted, but an instance can
only be created for a context the plugin declared through
`kOfxImageEffectPropSupportedContexts`.

## Action lifecycle

```
load -> describe -> describe-in-context -> create-instance -> render -> destroy
```

Two details are easy to get wrong and are load-bearing:

- **`describe` receives a real handle.** The OFX docs call the plugin-descriptor
  handle "redundant", but a plugin legitimately calls `getPropertySet` on it to
  publish `kOfxImageEffectPropSupportedContexts`, `...SupportedPixelDepths` and
  `kOfxPropLabel`. Passing `NULL` there breaks every real plugin, so the plugin
  slot is passed and is tagged as a plugin descriptor.
- **One handle, two meanings.** OFX reuses a single opaque `void *` for the
  plugin descriptor and for instances. Both reach the same suite entry points, so
  every handle this host hands out begins with a `jfx_ofx_owner_t` tag
  (`JFX_OFX_HANDLE_MAGIC` plus a kind). A NULL or untagged handle is rejected
  rather than cast, which is what stops a descriptor being read as an instance.

## Buffers and images

Images are **borrowed views**, never copies: the caller owns the source frame
and the destination buffer for the duration of a render. `clipGetImage`
publishes `kOfxImagePropData`, `kOfxImagePropRowBytes`, `kOfxImagePropBounds`,
`kOfxImagePropPixelAspectRatio`, `kOfxImageEffectPropPixelDepth` and
`kOfxImageEffectPropComponents`. `clipReleaseImage` is a flag flip; the
container is recovered from the embedded bag with an exact `offsetof`.

Rendering is **transactional**: output goes to a scratch frame and is copied to
the caller's destination only if the plugin returned success and did not abort.
This matches the rule the engine's CPU image path already follows, so a failing
plugin cannot leave a half-written frame.

## Parameters

Values are seeded from `kOfxParamPropDefault`, which plugins legitimately write
as an `int` for boolean/integer parameters and a `double` for floating-point
ones; `jfx_ofx_props_get_number()` reads either. Setters clamp to the declared
`kOfxParamPropMin`/`kOfxParamPropMax`, and a non-finite value is coerced to a
bound rather than propagated.

Parameter type is recovered from the declared component count, since OFX 1.5 has
no separate label property for a parameter (`kOfxPropLabel` is read first, then
`kOfxParamPropHint`, then the name).

## Bounds

Every limit is fixed and small, because a property bag is embedded in every clip
and parameter (`ofx_internal.h`): 24 properties per bag, 32 parameters, 8 clips,
64 instances, 64 plugins, 96-byte names, 128-byte strings, 4 string-array
entries. A plugin cannot make the host allocate by asking for a large value.

| API | Behaviour |
|---|---|
| `jfx_ofx_host_scan` on a missing directory | `JFX_ERROR_NOT_FOUND` |
| `jfx_ofx_host_scan` finding no bundles | `JFX_SUCCESS` (plugin dirs are often empty) |
| module exporting no `OfxGetPlugin`/`OfxGetNumberOfPlugins` | skipped, not loaded |
| plugin whose `pluginApi` is not `kOfxImageEffectPluginApi` | skipped |
| plugin declaring no supported context | skipped |
| wrong `size` on any desc struct | `JFX_ERROR_INVALID_ARGUMENT` |
| filter context with no `src` | `JFX_ERROR_INVALID_ARGUMENT` |
| destination untouched on failure | yes |

## Testing

`ofx_host` builds a **real** OFX bundle on disk from a **real** shared object
(`frontends/plugins/tests/gain_plugin.c`) and drives it through `dlopen`. Nothing
is stubbed, because the behaviour worth testing — symbol resolution, `setHost`,
suite vtables agreeing with the OFX headers, and pixel output — only exists when
the plugin is a separate binary.

The test covers invalid arguments on every entry point, discovery, description,
parameter metadata, pixel output, clamping, transactional failure, the General
context, and instance independence. It also asserts on real pixels rather than
merely on return codes, so a plugin that is never invoked fails the test.

Verified on Linux/x86-64: full native CTest **350/350**, and **350/350** under
ASAN + UBSan with leak detection, both with no compiler diagnostics.

## Known limitations

- **Frame rate.** OFX time counts frames; `jfx_ofx_render_desc_t` passes
  seconds as an opaque monotonically increasing value. Effects needing true frame
  numbers should use the timeline API.
- **`General` context extra inputs.** A plugin may declare additional input
  clips (a `Mask`, for example). This host only binds the standard source and
  output, so such a clip is declared but never populated. A plugin that reads an
  unbound optional clip sees `clipGetImage` fail rather than stale pixels.
- **No `is-identity`, `get-region-of-definition`, `get-time-domain` or
  `get-frames-needed` actions.** These are not required for a correct render of a
  non-tiled, non-animated CPU effect, and plugins that only trap them receive
  `kOfxStatReplyDefault`.
- **Windows/macOS discovery paths** are implemented (`Windows/x86-64`,
  `Windows/arm64`, `MacOS/x86-64`, `MacOS/arm64`) but have not been exercised
  here; only `Linux-x86-64` has been run.
- **Not integrated into a frontend.** The adapter is a library plus tests; no
  desktop/web/mobile surface exposes it yet, and no CLI command drives it.