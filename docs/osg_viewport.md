# OSG viewport bridge

Blends `third_party/openscenegraph` into the 3D Modeling & Animation viewport
without requiring a full viewer link.

## Why

The legacy viewport re-rendered the full CPU rasterizer (4x supersampled)
on every mouse-delta event. Gimbal drags therefore felt slow and jagged:
each `3d.orbit`/`3d.orbit_axis` command re-ran the whole scene at up to
1280px in the UI thread.

## What the bridge does

Public header `src/include/jfx/jfx_osg_viewport.h` (API 1.0), implemented in
`src/src/osg_viewport.cpp`:

- OSG-style trackball orbit (`jfx_osg_trackball_orbit`, `jfx_osg_axis_orbit`)
  over normalized XYZW quaternions, matching `osgGA::TrackballManipulator`
  axis rotation semantics.
- Velocity damping (`jfx_osg_damp_velocity`) for smoothing per-event deltas.
- Adaptive preview (`jfx_osg_preview_size`): half resolution while
  `scene_navigation` is true, full resolution on release. Halving each
  dimension quarters the supersampled raster work per drag event; this is
  the main gimbal smoothness fix.
- Sphere/frustum instance culling (`jfx_osg_instance_visible`) for
  cloner-heavy scenes.
- Wider gimbal pick radius (`14px` vs the legacy `9px`) so the thin colored
  rings are easy to grab.

The desktop `modeling3d_panel.inc` viewport uses adaptive size, 14px picking
with a hover highlight pass, per-frame delta clamping (10px cap ≈ 4°/axis),
and a status line showing `Viewport WxH | OSG headers/fallback`. The Core
`3d.orbit` / `3d.orbit_axis` commands themselves route through
`jfx_osg_trackball_orbit` / `jfx_osg_axis_orbit` (`src/src/modeling3d.cpp`),
so every frontend (desktop, CLI, web, mobile) shares the bridge math; large
out-of-range rotations fall back to the legacy inline form with identical
semantics.

## Build

Default is the GLM fallback (no OSG link, deterministic CPU preview):

```sh
cmake -S . -B build
```

Opt-in to a real `libosg` link (math-only `osg::Quat` path; no `osgViewer`/
`osgDB`/OpenGL linkage into JoltFX targets):

```sh
cmake -S . -B build -DJFX_USE_OSG=ON
```

`ON` configures and builds the vendored OSG 3.7.0 `osg` core target in
isolation via `ExternalProject` (its `uninstall` target and CMake 2.8 floor
never touch the main build) and links it into `jfx_core`. First builds take
a while (a few hundred translation units); later builds are incremental.
`jfx_osg_have_osg()` reports 1 in this mode, and `test_osg_viewport` covers
both modes, including locked cross-path equivalence constants so the two
math backends can never silently diverge (they use opposite composition
order internally).

`jfx_osg_have_osg()` reports 1 when built with headers, 0 otherwise.
`test_osg_viewport` covers trackball normalization, axis rotation,
damping, adaptive sizes, culling and pick radius in both modes.

## Limits

- The default preview/export path stays the Core CPU rasterizer so
  headless tests, PNG/video export and `.jfx` determinism are unchanged.
- A full `osgViewer` embedded viewer (GL context, scene-graph nodes,
  shadows, LOD) is future work; the bridge only uses OSG math types today.
- Web/mobile use the same adaptive-size contract through shared commands;
  their gesture routing is unchanged.
