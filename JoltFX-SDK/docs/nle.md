# NLE workflow facet

Include `<joltfx/sdk/nle.h>` and request CMake component `nle` (SDK 1.4).
The adapter re-exports `jfx/jfx_nle.h` API 1.0 without private/vendor types.
Link `JoltFX::jfx_core` from the full engine installation for implementation.

- `jfx_nle_run_script`: no-input Joltscript command vectors, one transaction,
  64 KiB source / 100,000 VM steps / 256 commands. Failure rolls back.
- `jfx_nle_relink`: exact-path replacement of all media uses in one undo step.
- `jfx_nle_analyze_pcm/file` and `sync_clap`: 0.1–60-second stereo analysis,
  libebur128 integrated LUFS/sample peak and transient-clap alignment. No clap
  or ambiguity is a successful measurement with `clap_detected=false`.
- `jfx_nle_transition`: premultiplied internal CPU dissolve or small-motion
  morph cut over straight RGBA8 inputs; failure leaves output untouched.
- `jfx_nle_import/export_otio` and `timecode`: bounded editorial OTIO JSON,
  OpenTime rational rescaling/drop-frame timecode. Import replaces the sequence
  and clears history; unsupported effects/mixing/timing are rejected. Arbitrary
  OTIO metadata is not preserved.
- `jfx_nle_probe`, `encoder_at`, `ffmpeg_version`, `convert_begin`: direct FFmpeg
  inspection and independent snapshot conversion. Use `jfx_export_step`,
  progress/state, cancel and destroy on one serialized owner thread.

Analysis and conversion preparation are synchronous; schedule them away from
interactive controls. Do not share a live editor with workers. Reacquire borrowed
timeline handles after load/history/cancel. Analysis structs require `size`.
Conversion jobs own settings/model snapshots, not the external media bytes.

FFmpeg support depends on `JFX_VIDEO_FFMPEG`; system libass captions depend on
`JFX_NLE_LIBASS` and pkg-config detection. PCM analysis, script edits, transitions
and OTIO remain available without these optional libraries. Encoder enumeration
does not guarantee compatibility with the chosen container or a GPU device.
Morph cut is a bounded local fit, not face-aware reconstruction; clap detection
is a transient heuristic, not general audio matching or drift correction.
