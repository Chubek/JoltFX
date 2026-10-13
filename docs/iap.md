# Ingagi Animation Pack (.iap)

Versioned binary container for the 3D Modeling & Animation tab's animation
event stream plus packed asset bytes. Packs are written after an Ingagi drive
(or hand edits) and played back by applying their stream onto a scene.

## Workflow

1. Drive the scene with Ingagi (3D tab "Ingagi drive" or Game Engine
   workspace). Example: `ingagi/examples/joltfx_3d_drive.ing` writes spin,
   bounce, radial cloner and RGB material keys through `engine_scene_*`.
2. Export the pack: 3D tab "Export .iap"
   (`jfx_desktop_frontend_export_iap`) or headless
   `joltfx iap export SCENE.jfx OUT.iap [ASSET...]`.
3. Play it back: 3D tab "Import .iap", headless
   `joltfx iap import IN.jfx PACK.iap OUT.jfx`, or the `jfx_iap_import` /
   `3d.iap_import` API. Import replaces the scene clock, cloner layouts and
   key streams (per object index; the pack must hold as many objects as the
   scene) in a single undoable step. Meshes, base transforms, materials and
   per-channel Joltscript drivers are left in place, so re-importing onto
   the scene a pack was exported from replays exactly what was authored.
4. Inspect with `jfx_iap_info` / `jfx_iap_validate` (desktop "Inspect .iap",
   `joltfx iap info|validate`) or the `ingagi/iap` Go package.

## Format (v1)

All integers little-endian. See `src/include/jfx/jfx_iap.h`.

```
"IAP1" u32 version(1) u32 fps u32 frames u32 objects
per object: u8 name_len + name, u32 cloner_mode, u32 instances, f32 spacing,
  u32 keys, per key: u32 channel, u32 frame, f32 value, u32 interp
  (keys sorted by (channel, frame))
u32 assets, per asset: u8 path_len + path, u64 size, raw bytes
u32 CRC32-IEEE over all preceding bytes
```

Bounds mirror `jfx_modeling3d.h`: <=64 objects, <=4096 keys/object,
<=16 assets, <=16MiB/asset, <=64MiB total. Export validates the scene first
and writes via temporary + atomic rename, leaving the destination untouched
on error.

## Ingagi library

- `ingagi/stdlib/ecs/joltfx_scene_ecs.ing` (`std.ecs` scene linkage)
- `ingagi/stdlib/shader/joltfx_material.ing` (`std.shader` material)
- `ingagi/stdlib/gameplay-animation-ui/joltfx_drive.ing` (animation/cloner)
- `ingagi/stdlib/assets-resources/joltfx_iap.ing` (packable drive)
- `ingagi/examples/joltfx_3d_drive.ing`, `joltfx_iap_pack.ing`

Desktop host callbacks (scalar ABI, all through shared editor history):

| Callback | Behavior |
|---|---|
| `engine_scene_cloner(o, mode, count, spacing)` | `3d.cloner` |
| `engine_scene_cloner_real(o)` | `3d.cloner_make_real` at transport time, returns count |
| `engine_scene_procedural(o, field)` | 0 generator, 1 instances, 2 mode, 3 smooth |
| `engine_scene_color(o, ch, v)` | `3d.color` |

Scene indices shift on removal/undo; scripts must keep ECS associations.
`test_iap` covers export/validate/corrupt-CRC/asset packing plus a two-object
round trip; `ingagi/iap` Go tests cover `Encode`/`Decode` round trips and
rejection paths (`Encode` mirrors the Core writer, enabling cross-language
pack interchange); `test_ingagi_tab` covers the four new callbacks plus
drive/export/info; `ingagi/native_extension_api/joltfx_3d_test.go` executes
every shipped drive against a stub scene, proving the stdlib runs end to
end. Key-line parsing in the exporter is locale-independent (no `sscanf`).
