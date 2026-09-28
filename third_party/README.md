# Third-Party Dependencies

## Included Libraries

### stb_image.h
- License: Public Domain
- Purpose: Image loading

### stb_image_write.h
- License: Public Domain
- Purpose: Image writing

See individual library headers for full license information.

### OpenColorIO 2 (optional linked dependency)
- License: BSD-3-Clause; upstream: AcademySoftwareFoundation/OpenColorIO.
- Purpose: production LUT file readers, discovered with `find_package(OpenColorIO 2)`.
- C ABI adapter: `joltscript/layers/glue/include/joltscript/color_io.h`.
- Enable with `JFX_COLOR_OCIO=ON` and an installed OpenColorIO CMake package.
  The configure log explicitly reports when it is missing. Formats are enumerated
  from the linked version's FileTransform registry, not a hardcoded promise.
- Import currently resamples extended transforms to a 65³ RGB LUT on [0,1],
  matching the existing display-referred engine. This is an approximation and
  does not preserve unbounded HDR transforms or all color-management metadata.

### FFmpeg (optional linked dependency)
- Purpose: local video sources in timelines and compositing graphs.
- Required modules: libavformat, libavcodec, libavutil, libswscale (pkg-config).
- License depends on the chosen FFmpeg build; the adapter itself requires no
  GPL-only codec. Packaging must retain the selected build's license notices.
- `JFX_VIDEO_FFMPEG=ON` detects the libraries; `jolt_video_io_available()` reports
  actual availability. Missing support returns NOT_IMPLEMENTED at rendering.
- The initial decoder seeks by timestamps, respects clip source in-points, and
  scales decoded video to the preview. Audio and video encoding are not included.
