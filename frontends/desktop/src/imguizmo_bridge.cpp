// Upstream owns process-static ImVectors but has no shutdown hook. Compile it
// here so we can release those ImGui-allocator buffers before Tilly shuts down,
// without forking the vendor or exposing its private types outside this unit.
#include "ImGuizmo.cpp"

void jfx_desktop_gizmo_shutdown() {
    ImGuizmo::Enable(false);
    ImGuizmo::gContext.mIDStack.clear();
    ImGuizmo::gContext.mViewManipulateStates.clear();
}
