# Non-linear editor

The NLE uses one shared editor session for tracks, clips, effect stacks, Color
Calibration and Color Grading. Desktop, CLI/terminal, web, Android, iOS and the
After Effects/Premiere/Resolve bridges use the same editing commands and `.jfx`
sequence documents.

## Quick start

Create a sequence with an exact rational frame rate, edit it, then export frames:

```sh
joltfx nle new sequence.jfx --size 320 180 --fps 30000 1001
joltfx nle edit sequence.jfx edited.jfx <<'EDITS'
clip.add 0 0 0 120
clip.name 0 0 0 0 First shot
clip.split 0 0 0 60
track.add 0 0 0 0 Video 2
clip.move 0 1 1 90
calibration.add 1 0 0 0 calib_lut
grade.add 1 0 0 0 grade_primary
grade.param 1 0 0 1 exposure
timeline
save
EDITS
joltfx nle info edited.jfx
joltfx nle render edited.jfx -o frame_ --start 0 --end 150
```

`nle new` creates an empty **V1** track. `nle render` writes
`frame_0000.ppm`, `frame_0001.ppm`, etc. Its range is **start-inclusive,
end-exclusive**; the default end is the sequence duration. An empty sequence
exports one transparent frame, written as black RGB in PPM. Add `--width W
--height H` to scale the sequence raster with nearest-neighbour sampling.
PPM exports discard alpha. Output directories must already exist.

`joltfx edit` and `joltfx nle edit` use the same line-oriented terminal editor.
`undo`, `redo`, `timeline`, `show`, `save` and `quit` are standalone commands.
`timeline` prints JSON; `show` prints the project. `save` writes the destination
and exits; `quit` exits without writing. Piped commands are suitable for
reproducible edits. A rejected command exits with an error before saving.

## Frontend controls

| Frontend | NLE and color workflow |
| --- | --- |
| **Desktop** | Open a `.jfx` project, select a clip, drag its body to move it or its edges to trim it, and scrub the ruler. Use zoom, first-frame/pan, Fit and Snap controls. Track mute/solo/order, rename, split, duplicate, slip, delete, ripple delete and gap insertion are available in Timeline. **Sequence setup / frame export** creates a raster/rate or writes the selected frame. Open **Color Calibration** and **Color Grading** from View to edit that same clip. |
| **CLI/terminal** | `nle new/edit/info/render`, the shared command reference below, and `calibration.*`/`grade.*` commands. `calibration` and `grade` list operators in the terminal editor. |
| **Web** | `JoltEditor` provides a canvas timeline with clip selection, drag/edge trim, ruler seeking, Fit and snapping. Scroll pans; Ctrl/Cmd+scroll zooms. Numeric controls expose edits and sequence setup. Import images/LUTs into WASM's filesystem, open/save `.jfx` documents, or download a PPM frame. Color sections follow the selected track/clip. |
| **Android** | `NLETimelineView` supports touch selection, ruler seeking, clip movement between tracks and edge trimming. The activity exposes numeric editing, undo/redo, project paths, sequence setup and frame export, with separate Calibration/Grading controls below the timeline. |
| **iOS** | Embed `JFXNLEViewController` with a `JFXMobilePlayerBridge` (UIKit, iOS 14+, ARC). It provides preview/playback, a frame scrubber, selectable track/clip lists, edit controls, project paths, sequence setup and frame export. Its color button opens `JFXColorViewController` for the selected clip. |
| **AE / Premiere / Resolve** | `jfx_host_nle_*` sessions expose the same commands, state JSON, project I/O, RGBA previews and PPM export to SDK adapters. The color descriptor/processing API is also available to all three hosts. Host SDK menu/panel registration remains in the host-specific adapter. |

Desktop timeline shortcuts are **Space** (play/pause), **S** (split), **Delete**
(delete), **Shift+Delete** (ripple delete), **Ctrl/Cmd+Z** (undo) and
**Ctrl/Cmd+Shift+Z** (redo). Web provides the editing shortcuts above, with its
toolbar controlling playback. Shortcuts are suppressed while entering text.

The desktop file-path popup has **Open** and **Save .jfx** buttons. File > Export
Frame uses the path entered in the timeline's frame-export controls.

## Shared commands

```c
jfx_editor_command(editor, op, a, b, c, value, text);
```

Indices are **zero-based**. The terminal syntax is `OP A B C VALUE TEXT`; omit
`TEXT` when unused. Frames and lengths must be integral. In the table, `—`
means an unused argument, conventionally zero (or an empty text string).

| Operation | A | B | C | Value | Text |
| --- | --- | --- | --- | --- | --- |
| `sequence.new` | width | height | FPS numerator | FPS denominator | — |
| `sequence` / `graph` | — | — | — | — | —; selects the preview/document mode |
| `track.add` | — | — | — | — | name |
| `track.remove` | track | — | — | — | — |
| `track.name` | track | — | — | — | name |
| `track.move` | track | — | — | destination track index | — |
| `track.mute` / `track.solo` | track | — | — | 0 off, nonzero on | — |
| `track.opacity` / `track.blend` | track | — | — | opacity / blend enum | — |
| `track.insert_gap` | track | — | insertion frame | gap length | — |
| `clip.add` | track | source enum | start frame | length | media path for image/video/audio |
| `clip.name` | track | clip | — | — | name |
| `clip.enabled` | track | clip | — | 0 off, nonzero on | — |
| `clip.opacity` / `clip.blend` | track | clip | — | opacity / blend enum | — |
| `clip.trim` | track | clip | new start frame | new length | — |
| `clip.move` / `clip.duplicate` | source track | clip | destination track | new start frame | — |
| `clip.split` | track | clip | — | split frame on sequence | — |
| `clip.slip` | track | clip | — | signed source-in delta | — |
| `clip.remove` / `clip.ripple_delete` | track | clip | — | — | — |
| `undo` / `redo` | — | — | — | — | — |

Sources are `solid=0`, `gradient=1`, `checker=2`, `sweep=3`, `image=4`,
`video=5`, `audio=6`. Native `jfx_clip_desc_t` supplies source parameters and an initial
source in-point; the compact `clip.add` command uses the default source colors.
Image/video/audio sources require a nonempty accessible path. Video decoding uses the
existing FFmpeg adapter when enabled with `JFX_VIDEO_FFMPEG`; unsupported or
missing media produces a render error.

Effects use the same stack as color editing. `effect.add/remove/move/enabled`,
`effect.param` (parameter name in text), `effect.path`, `effect.opacity` and
`effect.blend` address the **absolute effect index** in C. `grade.*` and
`calibration.*` address a **section-local index** instead; see
[color commands](editor.md#shared-editing-commands).

`effect.key.add` and `effect.key.remove` address track/clip/effect in A/B/C.
Text is `PARAM_INDEX SEQUENCE_FRAME`; value is the new key's scalar. These
commands convert the sequence frame to the clip's animation reference clock.

## Timing and edit semantics

- Clip coverage is `[start, start + length)`. Duration is the greatest clip end,
  including disabled clips and muted tracks. Empty areas render transparent.
- Tracks composite bottom-up: the higher track index is above the lower one.
  Within a track, later-created clips composite over earlier clips. Splitting
  inserts the right piece immediately after the left piece; duplication appends
  to the destination track.
- **Move** preserves length, source in-point and clip-relative animation.
  Moving to another track transfers ownership; **duplicate** makes independent
  copies of paths, parameters and key arrays.
- **Trim** advances source in-point and animation offset by the head's movement,
  retaining the original source/curve samples on frames that remain visible.
  Extending the head requires sufficient source handles. A tail-only trim changes
  length, keeping both clocks fixed.
- **Split** requires a frame strictly inside the clip. The right piece starts at
  that frame, advances its source-in and animation offset, and retains the complete
  interpolation curve. Linear, hold and smoothstep animation preserve their
  samples across the cut.
- **Slip** changes source in-point alone. Video frame selection follows
  `(sequence frame - clip start + source in-point) / FPS`. Procedural test-pattern
  sources retain the renderer's wall-clock animation behavior.
- **Ripple delete** closes the selected clip's interval on its track; subsequent
  clips move left by its length. It rejects any other clip intersecting the
  removed interval. **Insert gap** shifts clips starting at/after the insertion
  frame right; it rejects clips straddling that frame. Other tracks keep their
  positions. Animation follows moved clips.

Keys are stored in an original clip-relative reference clock:

```text
reference frame = max(0, sequence frame - clip start + key offset)
```

The offset starts at zero, advances on splits/head trims, and persists as
`clip_keys`. `jfx_timeline_effect_param_at` retains its original reference-clock
convention. `jfx_timeline_effect_param_on_timeline` evaluates a sequence frame
using the renderer's clock. Raw `jfx_timeline_add_key` accepts reference frames;
editor key commands accept sequence frames. Editor keys before the clip start or
with a negative reference frame are rejected.

## Persistence, history and adapters

Sequence saves retain exact integer timing and rational FPS, quoted track/clip
names and media/LUT paths, source in-points, mute/solo, clip enable/opacity/blend,
effect enable/opacity/blend/interpolation, parameters and keys. Floats are written
with enough digits to round-trip their stored values.

The additional positional directives are:

```text
track_state MUTED SOLO OPACITY BLEND
clip_state SOURCE_IN ENABLED OPACITY BLEND
clip_keys SIGNED_ANIMATION_OFFSET
effect_state ENABLED OPACITY BLEND INTERPOLATION
track_audio GAIN
clip_audio ENABLED GAIN PAN FADE_IN FADE_OUT REFERENCE_LENGTH
```

They apply to the most recently declared corresponding object. `clip_state`
sets an absolute source in-point. Existing `key`, `disable` and `opacity`
directives remain supported; their clip/effect indices are **one-based** in the
file. Old unquoted project tokens still load. Older readers that do not support
the new state directives may reject newly saved documents.

Command-based sequence and graph edits share up to **32 undo/redo steps**, with a **32 MiB
combined snapshot budget**. New edits clear redo; successful project loads clear
both stacks. Failed commands and failed loads preserve history. Snapshots restore
their edited document and prior active mode while retaining the other model.
See [composition history](composition.md). Direct mutations of a borrowed timeline
are outside history; call `jfx_editor_clear_history` after such mutations.
Reacquire borrowed timeline handles after load, `sequence.new`, undo or redo.
The session is single-owner-thread.

`jfx_editor_sequence_state` returns JSON containing raster, rational rate,
duration in frames, undo/redo availability, tracks and clips (names, source/path,
start, length, source in-point, enabled state, opacity and effect count). It
preserves the active document mode. Frontend wrappers and `jfx_host_nle_state`
provide the same representation. A buffer too small returns `OUT_OF_MEMORY`;
mobile/web widgets use a 4 MiB buffer.

`jfx_editor_render_frame` previews/exports an exact integer sequence frame,
including rational-rate sequences, with transactional caller-buffer output.
`jfx_editor_write_frame` writes PPM only after rendering succeeds, so a missing
media/LUT render leaves an existing output file intact. Native wrappers expose
frame export for desktop, mobile and hosts. Web's exact-frame export accepts a
32-bit frame number and downloads the bridge's preview-size raster.

Editor rasters/previews are bounded to **4096 per axis**, projects to **8 MiB**,
tracks to **16**, clips per track to **128**, effects per clip to **32**, and keys
per effect parameter to **64**. Core frame/source ranges fit `INT64_MAX`; the
compact command surface bounds numeric values to ±1,000,000,000 and A/B/C to
32-bit unsigned integers. Native timeline APIs expose wider frame ranges.

## Verification and versions

The additive editor API is **1.4**, timeline API **1.2**, and color API **1.0.0**.
Existing public structure layouts are preserved. `tests/fixtures/nle_sequence.jfx`
is the small cross-frontend sequence. Native tests cover ownership, edit timing,
ripple conflicts, limits, smooth-curve pixel preservation, history, rational-rate
rendering, persistence, shared frontend pixels/state and PPM exports. Web tests
cover hit testing, snapping, one-edit drag commits, tail clicks, exact-frame
bridging and mounted color/NLE controls.

Android/iOS UI builds require the platform toolchains; browser WASM builds
require Emscripten. Host-installed panels/plugins require the corresponding SDK.
The portable adapters are exercised by native tests. Rendering uses the shared
CPU graph/color reference path described in [color execution](editor.md#kernel-glue-and-execution-path).
The shared stereo mixer supports audio-only and video clips, gain/balance/fades,
mute/solo, resampling and source in-points. FFmpeg exports MP4/MOV/MKV with mixed
audio, immutable snapshots, incremental progress/cancellation and transactional
output. Audio/export APIs are **1.0**. See [media commands, builds and frontend
controls](media.md).
