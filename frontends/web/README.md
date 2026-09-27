# JoltFX web player

The dependency-free TypeScript player renders WASM-provided RGBA frames to a
canvas, uses `requestAnimationFrame` for playback, supports seeking/looping,
and accepts drag-and-drop package input. It enforces a 256 MiB package limit
and rejects cross-origin package URLs; host applications should proxy trusted,
CORS-validated content through their own origin.

`jfx_web_session` now renders bundled effects to caller-owned RGBA8 memory.
`EmscriptenJoltBridge` connects its exports to `JoltPlayer`. The beta transport
is a UTF-8 JSON envelope such as `{"effect":"brightness","parameter":0.1}`;
production `.joltpkg` decoding and signature verification should feed the same
validated effect request. Export the five `jfx_web_session_*` functions when
building with Emscripten. Build and run the browser-independent player tests:

```sh
cd frontends/web
npm test
```
