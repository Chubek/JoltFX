# Audio mixing, DAW and VST3

The desktop **DAW** tab provides seven views over the shared sequence:

- **Arrangement**: import audio through the Media path/Add clip controls, add
  audio tracks, select and move clips between lanes, trim either edge, snap to
  beats/clip edges/playhead, split, duplicate, slip and use undo/redo.
- **Mixer**: track faders, mute/solo, clip gain, stereo balance, fades, master
  gain, audio import with an explicit frame duration, and stereo playback peaks.
- **VST3 Inserts**: discover native plugins, add/reorder/bypass/remove per-track
  effects/instruments, edit generic parameters or open their native editor,
  and capture opaque plugin state.
- **MIDI / Instruments**: MIDI tracks/clips, a 128-pitch piano-roll display and
  note selection/add/update/delete controls for pitch, channel, velocity and
  source-frame timing. Notes feed the track's first enabled VST3 instrument.
- **Recording**: select an SDL input device and WAV destination, record stereo
  input from the playhead, then stop/add the take or cancel it.
- **Automation**: track-gain and VST3-parameter lanes with sequence-frame keys,
  editable values and linear, hold or smooth interpolation.
- **Mixdown**: incremental, cancellable stereo 32-bit float WAV export at 48 kHz.
  WAV output preserves floating-point headroom and is atomically installed only
  after successful completion. The export API also accepts 8–192 kHz.

The existing **Audio Mixing** workspace remains available. Its expandable VST3
track inserts section accesses the same racks. DAW and Audio Mixing share the
transport, clip selection, project file and editor history with NLE. Mute and
solo retain the sequence semantics: they affect both audio and video.

## Building

```sh
cmake -S . -B build -DJFX_FRONTEND_DESKTOP=ON -DJFX_AUDIO_VST3=ON
cmake --build build --target jfx_desktop
./build/frontends/desktop/jfx_desktop
```

Native desktop builds enable `JFX_AUDIO_VST3` by default. CMake downloads the
MIT-licensed Steinberg `vst3_pluginterfaces` at commit
`4f547e8e102b47de4a8b8aaf343c73b700786372`, with SHA-256 verification. It builds
only the ABI interface support sources; no additional GUI toolkit, audio driver
or full SDK is needed. For an offline build, use a checkout whose directory is
named `pluginterfaces`:

```sh
cmake -S . -B build \
  -DFETCHCONTENT_SOURCE_DIR_JFX_VST3_INTERFACES=/path/to/vst3/pluginterfaces
```

`JFX_AUDIO_VST3=OFF` avoids downloading/building the dependency. Browser, Android
and iOS builds default to OFF. They can preserve rack descriptors in project
files; rendering an enabled native insert returns NOT_IMPLEMENTED.

## Plugin workflow

1. Open **DAW → Mixer**. Add an audio track, enter a local audio file path and
   duration in frames, then **Add audio at playhead**.
2. Open **VST3 Inserts** and select the destination track.
3. Enter a `.vst3` bundle/module or a directory and click **Scan path**. Use
   **Scan installed VST3** to search standard installation locations instead.
4. Choose a factory class and click **Add VST3 insert**. The host validates its
   stereo processing support before inserting it.
5. Select a rack slot and click **Edit VST3 parameters**. Sliders show normalized
   values; stepped parameters quantize to their declared steps. Read-only
   parameters remain read-only. Drag gestures are one undo step.
6. Play to audition, or use **Mixdown → Bounce stereo WAV**. Video export with
   audio enabled uses the same inserts.

Discovery searches `~/.vst3`, `/usr/lib/vst3` and `/usr/local/lib/vst3` on Linux;
user/system `Library/Audio/Plug-Ins/VST3` on macOS; and Common Program Files VST3
plus Local AppData `Programs/Common/VST3` on Windows. Scans are explicit and
bounded to 128 factory classes, eight directory levels and 10,000 entries.
Plugins must match the running application's architecture.

## Processing and persistence

```
decoded clips → clip gain/balance/fades → summed track audio
  → ordered VST3 inserts → track gain → summed tracks → master gain → output
```

The Core mixer owns immutable copies of the rack settings. Live playback and
export snapshots own independent plugin instances. Module initialization is
reference-counted per canonical path; components/controllers are terminated
before unloading their module. Host-owned instances, buffers, streams, messages
and attributes use Tilly allocation. The host creates no worker threads and
processes on the mixer's single owner thread.

Stereo float32 effects with one main input/output bus and instruments with no
audio input and one stereo output are supported. MIDI note-on/off events carry
sample offsets, channels, velocities and stable note IDs. Instruments require
an event input bus and must occupy the first enabled rack slot. Audio clips on
the same track are added to the instrument's output, with matching delay, before
subsequent effects. Seeking chases notes already held at the requested position;
notes are clipped to the MIDI clip's visible source range. Clip gain scales MIDI
velocity (clamped to one); MIDI clip balance/fades are unsupported. Use plugin
envelopes and track automation for instrument shaping.
Combined and separate edit controllers, component/controller connections,
initialization state synchronization, host messages/attributes, normalized
parameter queues and tempo/4/4 transport context are implemented. Each rack can
hold eight inserts and 64 persistent parameter overrides per insert. Plugin
metadata may expose more parameters; only 64 distinct parameters can be saved
as overrides. Tempo is 20–400 BPM and uses the sequence's existing frame clock.

Rack latency is automatically compensated by priming the chain and reading
ahead, up to two seconds per rack. Contiguous blocks preserve effect state.
Latency changes during processing require a new rack configuration; a plugin
that changes its reported delay after activation fails explicitly rather than
silently misaligning the mix.
Seeking or rebuilding the mixer starts fresh plugin instances and resets tails;
mixdown ends at the sequence's requested frame range. Extend that range to
include a desired tail. Missing enabled plugins and unsupported layouts fail
explicitly; bypassed unavailable inserts do not prevent mixing.

Project serialization also stores `midi_note`, `audio_automation`/
`automation_key`, and `audio_insert_state`/`state_hex` records. Opaque JVS1 blobs
contain component and controller chunks, each bounded to 1 MiB. Chunked hex
records retain binary zeros and keep lines bounded. Restore state before static
parameter overrides. Parsing validates data without loading modules. Racks,
state, notes and automation participate in bounded sequence history and mixer/
export snapshots. The existing 8-MiB project and 32-MiB history limits apply.
Sequence-state JSON exposes MIDI notes, automation keys and insert state sizes.

## Native editors and plugin state

Select an insert and click **Open native VST3 editor**. The host creates an SDL
container and attaches the plugin's own `IPlugView`. Parent types are HWND on
Windows, NSView on macOS and X11EmbedWindowID on Linux/X11. On Linux a native
Wayland SDL window has no VST3 parent type; launch with `SDL_VIDEODRIVER=x11`
when an X11/XWayland display is available. Headless builds use generic controls.
Plugins without a native view retain the generic inspector.

Resize/focus/keyboard/wheel events route to the view. Linux timers and readable
descriptors are serviced by the serialized UI event loop. The view is removed
before its parent, controller or module is destroyed. Native parameter gestures
and dirty/preset notifications capture state, clear stale overrides and group
undo; playback rebuilds its snapshot on changes. Save/export synchronize pending
edits. **Capture plugin state** also saves a generic inspector's processor and
controller state. Native state requires the plugin to implement `getState`.
Plugin-owned references to sample files remain references; `.jfx` does not copy
external sample libraries or load standalone `.vstpreset` files.

## Recording and automation

In **Recording**, choose the destination, input and track, position the playhead,
and click **Record input**. Capture uses queued SDL float-stereo audio at 48 kHz
and an incremental WAV writer, with no FFmpeg dependency. The playhead follows
captured sample count; sequence playback stops during capture. **Stop and add
take** finalizes/publishes the WAV and adds a clip in one undo step. Its duration
rounds up to a sequence frame. **Cancel take**, device failure or a capture queue
overrun removes incomplete output. Existing output is replaced only on successful
completion. Undo removes the clip while retaining the completed WAV. Changes to
the sequence are blocked during a take. Input monitoring, overdub playback,
live MIDI-device capture and multitrack recording are not provided.

Automation supports 64 lanes per track and 128 keys per lane. In **Automation**,
choose track gain or an insert/parameter ID, then add/update keys or delete the
selected frame. IDs are shown in the VST3 inspector. Automation replaces the
static value, holds the first/last value outside the key range and follows
inserts through rack reorder/removal. Track gain is evaluated per sample; VST3
linear/hold curves use offset parameter queues, while smooth curves use bounded
piecewise-linear sampling. Plugin automation accounts for preceding insert
delay. Playback and WAV/video export use the same curves.

Sidechains, multichannel routing, MIDI CC/pitch-bend and MIDI-file import are not
implemented. macOS/Windows native paths are implemented; verification here used
Linux/X11 and the included VST3 fixtures.

## API and checks

`jfx/jfx_vst3.h` provides the portable insert model and native discovery,
instance, native-view, state, parameter and event-processing APIs. MIDI,
automation and recording APIs are `jfx_midi.h`, `jfx_automation.h` and
`jfx_recording.h`. Editor API 1.7 includes:

| Command | Arguments |
| --- | --- |
| `audio.insert.add` | `a=track`, `text="32-hex-CID path"` |
| `audio.insert.remove` | `a=track`, `b=insert` |
| `audio.insert.move` | `a=track`, `b=insert`, `c=destination` |
| `audio.insert.enabled` | `a=track`, `b=insert`, `value=0/1` |
| `audio.insert.param` | `a=track`, `b=insert`, `c=parameter ID`, `value=normalized` |
| `audio.insert.state` | `a=track`, `b=insert`, `text=JVS1 hex` (empty clears) |
| `audio.insert.clear_params` | `a=track`, `b=insert` |
| `midi.note.add/set` | `a=track`, `b=clip`, `c=note index` for set, `value=velocity`, `text="pitch source-frame length channel"` |
| `midi.note.remove` | `a=track`, `b=clip`, `c=note index` |
| `audio.automation.key` | `a=track`, `b=insert`, `c=parameter ID`, `value`, `text="gain|plugin frame interpolation"` (0 linear, 1 hold, 2 smooth) |
| `audio.automation.remove` | same indices, `text="gain|plugin frame"` |
| `audio.master.gain` | `value=linear gain [0,16]` |
| `audio.tempo` | `value=BPM [20,400]` |

Export API 1.1 accepts `.wav`/`container="wav"` with `audio=true` and a sequence.
It ignores video and codec options and does not require FFmpeg. RIFF output is
limited to less than 4 GiB. The existing incremental job/progress/cancel API is
used for WAV and encoded video alike.

`vst3_audio_host` loads a real test module and checks factory discovery, host
messages, combined/separate controllers, parameter changes, unsupported layouts,
multiple instances, latency alignment, discontinuous/contiguous blocks, snapshot
lifetime, project/history round trips, and WAV completion/cancellation/failure.
`daw_ui_tests` drives real ImGui input through the browser, insertion, parameter
gestures, state capture, MIDI editing, automation and bypass. `workspace_ui_tests`
includes DAW tab selection. `daw_notes_state_recording` exercises source-note
history, chunked state round trips, rack/automation ownership, gain curves and
WAV capture/cancel/decoding. `daw_native_window_capture` attaches a native view
through a real SDL window and records from SDL's deterministic dummy input.
