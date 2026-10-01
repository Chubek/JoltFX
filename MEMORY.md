# JoltFX — working memory

Last updated: 2026-10-01. Written at the end of the "implement the rest of the
remaining kernels" task. Resume by reading this file plus `PROGRESS.md`.

## Where things stand

- **294 registered image-profile kernels**, covering **297 of 322** CSV rows in
  `kernels/JoltFX-Kernels.csv`. **0 passthrough stubs.**
- In-tree `build/`: `ctest` **323/324**, **0 warnings**. The single failure,
  `compose` (`kind 'video' did not render: -5`), was confirmed to fail identically
  at pristine HEAD with the tree stashed — pre-existing and unrelated.
- **Work is uncommitted.** `git status` shows ~64 new and 8 modified paths.
- The legacy 12-effect JBC1 catalogue in `kernels/color/` is a *separate* execution
  path, deliberately not in `IMAGE_KERNELS`, and exempt from image-profile audits.

## The 25 unimplemented CSV rows are intentional

This profile's only output is an image, so these cannot be expressed:

- **17 non-image outputs** — `AudioBuffer` (`nle_audio_crossfade`,
  `nle_audio_ducking`), `Mesh` (`extrude_path`), `ParticleSystem` (5 `particle_*`
  except `particle_trail`), `Path` (`track_planar_tracker`), `Point[]` (5), `Vec2[]` (3).
- **8 scalar/vector outputs** — 6 `track_*`, 2 `audio_*`. Publishing a global
  estimate as a uniform image changes the output contract; that is an API-owner
  decision, not something to smuggle in.

`scripts/audit-kernels.py` lists both groups by name. `optical_flow` and
`track_motion_tracker` are the FlowField rows that *are* implemented, as 2-channel
images (c=0 is u, c=1 is v).

## Things that will bite you immediately

### Language rules (the profile is small and unforgiving)

| form | arity |
|---|---|
| `+` `-` `*` | variadic |
| `/` `min` `max` `and` `or` | **exactly 2** |
| `sample` | **exactly 5** — `x y c interpolation border` |
| `let` / `if` / `sum` | 2 / 3 / 4 (`sum` end is exclusive) |

- `def` does not exist. `defn` recursion works, depth-bounded.
- **`let` bindings evaluate eagerly.** This caused two real bugs: a division by a
  zero determinant *before* its guard, and a "stride" optimisation that zeroed
  samples inside the body and therefore computed every sample anyway. To reduce
  work you must shorten the `sum` ranges, not filter the result.
- A misspelled function and `(1 t)` (instead of `(* t 1)`) both report
  `unknown function`.
- **No fold and no tuples.** Min/max/nearest become log-sum-exp or exponentially
  weighted averages (`alpha-min`, `worley-soft`, `soft-max-luma`); a 3-vector
  becomes three scalar functions that recompute the shared part.
- An integer-arity param (`flag` = 1) with a fractional default is a hard parse error.

### Architectural cost limit

A kernel runs per `(x,y,c)` and cannot share work between pixels, so a whole-frame
reduction is `O(frame area)` **per pixel** — quadratic. `histogram_compute`,
`optical_flow` and `track_motion_tracker` are correct, `@gpu No`, and unusable at
delivery resolution. A *global* search is also biased if probes leave the frame
(they read 0), so inset the probe region by the search radius.

## Tools built during this task (reuse these)

- **`scripts/audit-kernels.py`** — the central audit. Checks all sources for
  compilation, metadata, param count/range/unused, passthrough bodies,
  category-folder agreement, test presence, and `IMAGE_KERNELS` vs the `tests/unit`
  verify-list parity. Run it after *any* library edit; it is what caught me
  deleting 15 helper functions.
- Throwaway C harnesses (rebuild against `build/kernels/libjolt_effects.a`):
  runtime check at the harness's exact limits, behavioural check for
  passthrough/constancy, and per-layout resource checks. The behavioural harness
  uses 4×4, `memory_limit=1024`, `step_limit=10000000` — a kernel that blows the
  step budget must be caught there, not in ctest.
- `/tmp/KERNEL_BRIEF.md` pattern (a written brief + one verified reference kernel per
  family) is what made five parallel subagents produce consistent output.

## Verification discipline that mattered

- **Compile-checking is not enough.** Bugs found only by executing: `calib_white_balance`
  returned red for green *and* blue; `remap_range` was an identity ignoring all five
  params; `temporal_exposure_blend` and `track_motion_tracker` were de facto
  identities; `grade_rgb_curves` failed on all real curve data because
  `curve-x-at` read the point *count* as the first x.
- **Assert a constant image survives every blur exactly.** A gradient source cannot
  detect a wrong divisor — this is the assertion that caught `lens_blur`, `tilt_shift`
  and `box_blur`.
- **Give `near()` a failure message.** A bare `assert` in a 400-line harness names no
  assertion; adding `near_ctx()` turned a 20-minute bisect into one build.
- **The behavioural harness passes no resource array**, so a resource-driven kernel
  only ever exercises its empty fallback. Resource-layout tests were added for
  curves, packed curves, ColorMap, cube LUT, a frame, a plane and a text run.
- **When a check fails, measure before theorising.** I blamed a stale library, then a
  build-dependency bug, then a design issue; the real cause each time was my own
  uninitialised test array. And `cmake --build ... | tail -1` hides the recompile
  lines, which made a healthy build look stale.

## Known follow-ups (not done)

- 30 unused params and 9 param-count findings remain, **all in the original
  20-kernel batch** (`transform/*`, `compositing/blend|screen|overlay|multiply|add|
  difference|alpha_composite`, `utility/*`, `generative/*`, `light/bloom`,
  `blur_sharpen/lens_blur`, `color/hue_rotate`, `time/*`, `transition/cube_flip`,
  `geometry/offset_path`, `stylize/stylize_oil_paint`, `noise/*`,
  `data_analysis/motion_detect`). `utility/remap_range` is the worst: 5 unused params.
  A separate cleanup pass.
- A **fresh out-of-tree configure** fails in `tests/unit/core/test_plugin_module.c`
  (`jfx/jfx_plugin.h` not found; the header lives in `mograph/include/jfx/`). This is
  pre-existing and unrelated to kernels, and the in-tree `build/` does not hit it —
  but it means "verify from scratch" currently only works via the in-tree dir.
- `obj_render` is a working ray-caster but a real triangle rasteriser is still worth
  building; `text_*` consume a rasterised coverage plane rather than a font, which is
  an architectural choice to revisit with the text engine.
