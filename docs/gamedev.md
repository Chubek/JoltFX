# GameDev workspace

GameDev combines GameMaker-style room/event authoring with a Godot-style shared
scene viewport and object inspector. **Ingagi is the gameplay language**: room
builders, reusable **Clichés** (labeled **Cliches** in the UI), prefab variants,
ECS composition and event logic are ordinary editable Ingagi functions.

Enable execution with `cmake -S . -B build -DJFX_INGAGI=ON`, then build
`jfx_desktop`. Without Ingagi, scene/source authoring remains available and the
execution controls are disabled.

## Workflow

1. Open **GameDev → Ingagi → Install starter…** and confirm replacement, or
   create a Cliche from the default primitive/pose/material builder.
2. In **Cliches**, choose a factory, enter an XYZ placement offset and select
   **Instantiate Cliche**. Each invocation runs real Ingagi code. Change the
   draft name and use **Create variant from Cliche** to delegate to a base
   factory and override the returned root's pose/material.
3. In **Rooms**, create a `room_*` factory from a Cliche. Edit it in Ingagi to
   compose a level. **Build room** appends objects; clearing the shared scene
   requires confirmation. Source is retained.
4. **Objects** selects, renames, duplicates, deletes and edits independent
   instances. The shared 3D viewport provides navigation and TRS handles. Use
   the 3D workspace for custom geometry; GameDev is not a separate scene copy.
5. **Play game** reloads source and calls `ready()` once, then `step()` every
   draw. Pause and single-step preserve script state; Play resumes a paused run.
   Stop resets the lifecycle, not scene edits. Set different zero-argument event
   names in **Debug**. The scene's animation transport is a separate clock.
6. **Animation** scrubs the scene and authors per-channel keys/interpolation.
   Ingagi can also write/sample keys with `engine_scene_key`/`engine_scene_sample`.
7. **Resources** saves/loads `.ing` source and `.jfx` scene snapshots separately,
   imports/exports PLY meshes and exchanges `.iap` animation packs. Save both
   files to retain code plus manually edited geometry/animation. Loading source
   never automatically executes it.

## Cliche contract

Any top-level zero-argument `cliche_*` function appears in the Cliche picker.
The builder emits an import-free function which creates a selected primitive,
sets TRS/tint, and returns its scene object index. For example:

```ingagi
fn cliche_Crate() {
    let object = engine_scene_add(0);
    engine_scene_set(object, 0, engine_spawn_offset(0));
    engine_scene_set(object, 1, engine_spawn_offset(1));
    engine_scene_set(object, 2, engine_spawn_offset(2));
    return object;
}

fn room_Warehouse() {
    let first = cliche_Crate();
    let second = cliche_Crate();
    engine_scene_set(second, 0, 3.0);
    return first;
}
```

Factories may create several objects, call other Cliches, add Ingagi ECS
components, or create animation keys. Variants must call a factory returning a
valid root index; their generated overrides affect that root only. The primitive
builder copies a selected object's pose/material, **not its custom mesh, keys,
mass, generator settings or scripts**. Edit the source to customize behavior.

Source edits affect future spawns. Instances are independent scene objects;
automatic propagation to existing instances, linked override tracking and
Unity-style Apply/Revert are not implemented. Object indices shift on deletion;
they are not ECS entity IDs. Keep associations under script control.

## Runtime hosts

Existing `engine_scene_*` hosts use Core commands/history. Transform channels
0–8 are position XYZ, Euler rotation XYZ (degrees), scale XYZ. Primitive indices
0–16 follow the Cliche builder's list. New scalar hosts:

| Function | Meaning |
| --- | --- |
| `engine_delta()` | Current preview tick in seconds; zero outside a tick |
| `engine_game_time()` | Elapsed simulation time; resets on a new Play lifecycle |
| `engine_time()` | Existing shared scene transport clock |
| `engine_input(action)` | Whether an action is held this preview tick |
| `engine_pressed(action)` | Rising-edge action press this preview tick |
| `engine_spawn_offset(channel)` | XYZ offset during Cliche instantiation only; otherwise zero |

Actions: **0 left** (A/Left), **1 right** (D/Right), **2 up** (W/Up), **3 down**
(S/Down), **4 jump** (Space), **5 accept** (Enter). Input is enabled in Rooms
only, not in script/resource editors or while typing/modifier shortcuts. Single
step advances 1/60 second with neutral input. Live delta is capped at 0.1 second;
this is a variable-step editor preview, not a fixed-timestep physics engine.

## Safety and current scope

Source must be nonempty, import-free and smaller than 16 KiB. Execution uses the
Ingagi VM with 100,000 instructions per load/call and 256 call frames. Pickers are
bounded lexical conveniences (up to 64 zero-argument entries with 63-byte names),
not semantic validation; compilation remains the authority. Source generation
validates names/numbers/capacity before appending and leaves source unchanged on
failure. A failed reload preserves the previous VM. Scene/host side effects are
not transactional across a script call and survive errors.

Runtime failures stop simulation and show diagnostics above the scrolling editor.
Source replacement, reload, project lifecycle, workspace changes and external
scene/history edits stop the run. Pause clears input. Save scene snapshots before
experiments; Undo/Redo restores individual Core commands, not a whole Play session.
Printing is discarded by the scalar embedding ABI; returned scalar values and
errors are displayed. Scene rendering remains scheduler-owned and budgeted.

This release provides a **3D authoring/play preview**, not a complete standalone
game engine. Tilemaps/2D room rendering, collision callbacks, runtime audio,
breakpoints, GPU gameplay/shader dispatch and packaged-game export are not wired
here. DAW and the 2D animation workspace remain separate tools. Ingagi native
QBE/WASM and shader artifact compilation remain CLI workflows.

Validation: `game_authoring`, `game_ui_tests`, `ingagi_tab` (including both
`JFX_INGAGI=ON` and optional-off builds).
