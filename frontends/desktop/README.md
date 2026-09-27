# JoltFX desktop frontend

The desktop frontend owns an engine and builds an editor frame with Dear ImGui:
a menu bar, viewport, timeline, properties panel, and console.

```bash
cmake --build build --target jfx_desktop
./build/frontends/desktop/jfx_desktop --headless-smoke --backend vulkan
```

The current executable validates the UI composition without a display server.
It does not yet create a native window or present ImGui draw data. The library
API supports opening an in-memory project path, resize, play/pause, seek, and
one UI frame, which keeps those interactions testable before a platform shell
is attached.
