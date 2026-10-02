# Audio mixing and encoded video export

The shared timeline mixer and editor exporter are used by desktop, CLI/terminal,
web/WASM, Android, iOS and the AE/Premiere/Resolve host bridges. `.jfx` documents
retain audio settings alongside frame-accurate NLE and composition edits.

## Dependencies and building

- **miniaudio 0.11.23** (`third_party/miniaudio`) decodes WAV, FLAC and MP3 to
  stereo float. The build disables its device, engine, resource-manager and
  threading layers; frontends send the shared mix to their platform audio API.
- **FFmpeg release/8.0** (`third_party/ffmpeg`) provides container demuxing,
  additional audio/video decoders, `swresample` audio conversion, `swscale` pixel
  conversion, video/audio encoding and muxing. The Git submodule records the
  exact revision. Third-party types stay inside Glue.

```sh
git submodule update --init third_party/miniaudio third_party/ffmpeg
cmake -S . -B build-media -DJFX_VIDEO_FFMPEG=ON -DJFX_MEDIA_FFMPEG_BUNDLED=ON
cmake --build build-media --parallel
```

Native builds default to `JFX_VIDEO_FFMPEG=ON`, discovering `libavformat`,
`libavcodec`, `libavutil`, `libswscale` and `libswresample` development packages
through pkg-config. `JFX_MEDIA_FFMPEG_BUNDLED=ON` builds the vendored source
offline with the configured compiler and GNU make. This dependency-free profile
supports native, Emscripten, Android arm64/x86-64 and single-architecture iOS
builds. It includes MPEG-4, AAC, ProRes, FFV1 and PCM encoders plus common input
decoders, with networking and codec threads disabled. It needs no FFmpeg command
line executable at runtime. System FFmpeg builds can expose more encoders.

`JFX_VIDEO_FFMPEG=OFF` retains WAV/FLAC/MP3 mixing; encoded export reports
`JFX_ERROR_NOT_IMPLEMENTED`. `joltfx capabilities`, `jfx_export_available()` and
`jfx_export_codec_available(name, audio)` report the linked build's capabilities.

## Add and mix audio

Source enum **6** is audio-only; source **5** is video with audio enabled by
default. Audio-only clips contribute no pixels. Other source kinds have audio
disabled. Paths are local files (WASM uses its virtual filesystem).

```sh
joltfx nle new sequence.jfx --size 320 180 --fps 30000 1001
joltfx nle edit sequence.jfx mixed.jfx <<'EDITS'
clip.add 0 0 0 120
track.add 0 0 0 0 Music
clip.add 1 6 0 120 /media/music.wav
clip.audio.gain 1 0 0 0.75
clip.audio.pan 1 0 0 -0.25
clip.audio.fade_in 1 0 0 15
clip.audio.fade_out 1 0 0 20
track.audio.gain 1 0 0 0.8
save
EDITS
joltfx export-video mixed.jfx -o final.mp4
```

All controls use the shared editor command form `OP A B C VALUE TEXT`:

| Operation | A | B | Value |
| --- | --- | --- | --- |
| `clip.audio.enabled` | track | clip | 0 off, nonzero on |
| `clip.audio.gain` | track | clip | linear gain, 0..16 |
| `clip.audio.pan` | track | clip | stereo balance, -1..1 |
| `clip.audio.fade_in` / `clip.audio.fade_out` | track | clip | integral frame length |
| `track.audio.gain` | track | unused | linear gain, 0..16 |

C and text are unused. Gain is independent of visual opacity. Center balance
preserves both channels; positive pan attenuates the left channel and negative
pan attenuates the right. Fades are linear, using the minimum of the fade-in and
fade-out envelopes when they overlap. Split/head trim retains the original
reference length and `clip_keys` offset, preserving the remaining audio samples
and fade clock. Source slip changes the source in-point; track mute/solo and
clip enable apply to audio as well as video. All commands participate in history.

`track_audio GAIN` and `clip_audio ENABLED GAIN PAN FADE_IN FADE_OUT REFERENCE_LENGTH`
apply to the most recently declared track/clip in `.jfx`. Source lines place the
quoted media path before start/length and the eight source parameters. For example:

```text
size 320 180
fps 30000 1001
track "Music"
track_audio 0.8
clip audio "/media/music.wav" 0 120 0 0 0 0 0 0 0 0 "Music bed"
clip_audio 1 0.75 -0.25 15 20 120
```

The mixer emits **interleaved stereo float** at 8,000..192,000 Hz in blocks of
1..65,536 frames. `jfx_timeline_audio_sample` computes the exact ceiling of
`frame * fps_den * sample_rate / fps_num`, with checked overflow. Signed source
and reference-clock offsets preserve split/trim timing at fractional frame rates.
Gaps and source EOF are silent; overlapping clips sum with headroom, without
clipping. Encoding clamps the mix to [-1,1]. Video with no audio stream is silent;
assigned missing/invalid media or unavailable decoders report an error.

`jfx_audio_mixer_create/render/destroy` retains an immutable copy of timing,
controls and media paths, usable after timeline edits/destruction. External media
bytes remain files. Readers open lazily, stream bounded blocks and close when
inactive. Arbitrary seeks and contiguous blocks use the same sample clock.
`jfx_timeline_render_audio` is a one-block convenience function. Capacity is a
float count; caller output is preserved on error. Mixers are single-owner-thread.

The new `kernels/audio_reactive/audio_mix.jolt` supplies gain, balance, dual-fade
and accumulation math. Glue compiles it to JBC1; Execution runs the validated
kernel over stereo frames and publishes output atomically. Existing image/color
kernels produce the video frames through the shared editor renderer.

## Encoded export

```sh
joltfx export-video mixed.jfx -o final.mp4 --start 30 --frames 90
joltfx nle export mixed.jfx -o master.mov
joltfx export mixed.jfx -o lossless.mkv --no-audio
joltfx compose export composition.jfx -o animation.mp4 --frames 300
```

Ranges are start-inclusive with a frame **count**. Count 0 exports the remaining
sequence; composition export requires a count and defaults to 30/1 fps. Sequence
export retains its exact rational rate. Output raster defaults to the project;
`--width W --height H` overrides it. Use `--codec ENCODER`, `--audio-codec ENCODER`,
`--sample-rate HZ`, `--container NAME` and `--no-audio` for explicit settings.

| Extension / container | Default video encoder | Default audio encoder |
| --- | --- | --- |
| `.mp4` / `mp4` | `mpeg4` | `aac` |
| `.mov` / `mov` | `prores` | `pcm_s16le` |
| `.mkv` / `matroska` | `ffv1` | `pcm_s16le` |
| `.webm` / `webm` | `libvpx-vp9` | `libopus` |

The bundled profile supports the first three rows. WebM and encoders such as
`libx264` require a linked FFmpeg build that supplies them. Unsupported codecs,
rates, dimensions or container combinations fail explicitly. Rasters are bounded
to 4096 per axis, frame counts to 10,000,000 and export FPS to 1..240. Codecs can
impose narrower bounds, such as even dimensions for subsampled formats.

Alpha-capable formats retain alpha (the default FFV1 path uses BGRA). Opaque
formats flatten straight RGBA over black before conversion. Sequence exports
include mixed stereo audio unless disabled; compositions omit audio. Native
`jfx_export_options_t` also supplies graph FPS and video/audio bitrates.

`jfx_export_begin` owns an immutable document snapshot. `jfx_export_step(job, n)`
processes up to 1..1024 frames, updating completed/total counters. Cancel or
destroy releases codecs and removes the temporary output. The destination is
replaced only after codec flush, mux trailer, stream flush/close and a successful
same-directory rename. Failed rendering/decoding/encoding preserves an existing
output file. `jfx_editor_export_video` runs a job to completion with a progress
callback returning false to cancel. Jobs run synchronously on their owner thread;
frontends step one frame per event-loop turn, and scheduler users may dispatch
their own task. An individual large CPU frame can take longer than a UI tick.

## Frontends and verification

- **Desktop:** timeline audio controls; SDL2 queued stereo playback; encoded
  export path, range, encoder, audio toggle, progress and cancellation.
- **CLI/terminal:** audio commands/history, `export-video` and `export`,
  `nle export`, `compose export` aliases with stderr progress.
- **Web:** AudioContext playback from the shared mixer; virtual media imports;
  incremental export and download. Enable bundled FFmpeg in the WASM build.
- **Android:** AudioTrack PCM-float playback; JNI audio/export handles; audio
  controls and incremental export to app-accessible paths. Pause cancels exports
  and releases queued audio. The app includes the bundled FFmpeg profile.
- **iOS:** AVAudioEngine playback; NLE audio controls and sequence/composition
  export stepping, cancellation and application/view lifecycle cleanup.
- **Hosts:** `jfx_host_nle_audio_mixer` and `jfx_host_nle_export_begin` share the
  engine contracts, including composition sessions (the session type is shared).
  SDK adapters connect them to the host audio output and export UI.

Native and sanitizer media tests cover fades, fractional-rate split/trim,
overlap, mute/solo, resampling, arbitrary seeks/EOF, snapshot isolation, PCM/raster
round trips, progress/cancellation and transactional failures. Frontend
conformance exercises desktop/mobile/web and all three hosts. Independent
FFmpeg/ffprobe integration checks codecs, nonzero audio, channel balance and A/V
timing. `frontends/web` real-WASM tests exercise mixing, filesystem resources,
codec output and cancellation through the production bridge. Apple UI validation
requires Xcode/iOS SDKs; Android playback validation requires a device/emulator.
