# Host application bridges

JoltFX ships SDK-independent bridge libraries for After Effects, Premiere Pro,
and DaVinci Resolve. They expose stable identifiers and the import, export, and
effect feature contract while keeping proprietary host SDK code out of the
portable build.

The `requires_host_sdk` flag is intentionally set until a host-specific build
is configured. The bridges are not installable Adobe or Fusion binaries by
themselves; a release build must link the relevant SDK adapter and be tested
against the host application's published SDK version.

Color processing is functional without an SDK: `jfx_host_color_kind_count` and
`jfx_host_color_kind_at` enumerate distinct Calibration/Grading groups, and
`jfx_host_color_process` applies the selected kernel to straight float RGBA.
Hosts pass initialized node values and an optional borrowed LUT. Native tests
exercise all three bridges. Host UI registration still belongs to the SDK adapter.
See [the shared color API](../../docs/editor.md).

## 3D scenes

`jfx_host_scene3d_create/state` exposes a portable 3D workspace for each host.
Use the session's load/save/edit/render/write_frame/export functions and `3d.*`
commands to author and animate meshes; scene JSON drives host panel controls.
Proprietary host UI registration stays in its SDK adapter. See
[3D usage](../../docs/modeling3d.md) for geometry, physics and rendering limits.

## NLE sessions

All three hosts also expose `jfx_host_nle_create/destroy`, `load/save`, `edit`,
`state`, `render` and `write_frame`. SDK panels can draw the shared JSON sequence
state, issue frame-accurate edits and undo/redo, apply Calibration/Grading commands
to selected clips, preview RGBA8 and export PPM frames. Documents are the same
`.jfx` sequences used by desktop/mobile/web/CLI. Host-independent tests exercise
each host's editing, color processing, load/history and frame export.

See [NLE commands, clocks and persistence](../../docs/nle.md). These APIs are
available in the portable bridge libraries; installing menus/panels requires the
corresponding host SDK adapter.

`jfx_host_nle_audio_mixer` returns an immutable stereo mixer for host playback.
`jfx_host_nle_export_begin` creates the shared incremental encoded job with audio
for sequences; it also accepts the shared composition session type for silent
graph export. Host adapters step jobs, display progress and release them on
cancel/close. Media conformance exercises all three hosts. See
[audio/export APIs and dependencies](../../docs/media.md).

## Composition sessions

AE, Premiere and Resolve all expose `jfx_host_composition_create/destroy`,
`load/save`, `state`, `edit`, `render` and `write_frame`. These reuse the shared
editor, typed DAG and bounded graph/sequence history. Build node libraries and
inspectors from `jfx_node_catalog`; use graph-state JSON for persistent layout,
values, wires, output and history flags. Render an interior node or use
`UINT32_MAX` for the output, and export PPM without changing the selection.

All three portable host sessions are covered by graph-state/RGBA/export
conformance and project-load/history tests. SDK adapters provide installed
panels and registration. See [composition commands and integration](../../docs/composition.md).
