# 2D animation runtime

The animation tab uses `jfx_animation_scene_t` as its renderer-neutral model.
Bones store local transforms and parent IDs; evaluation samples continuous-time
keys, applies step/linear/smooth curves, and propagates world transforms from
roots to leaves. A frontend can drive `jfx_animation_tab_tick` from SDL's
monotonic clock or from a browser animation callback and render the resulting
pose through its own backend.

`jfx_animation_compile` emits the deterministic, little-endian JFA1 payload.
The payload contains a versioned header, skeleton transforms, and keyframes;
`jfx_animation_validate` checks its bounds before `jfx_animation_play` decodes
and evaluates it. The same payload is the input contract for the native LIEF
section writer and the WASM player, so neither target needs editor objects.

The current API deliberately keeps rendering and asset resolution outside the
evaluator. That lets SDL, video export, native runtime stubs, and browser
canvas/WebGPU adapters share exactly the same pose and bytecode semantics.
