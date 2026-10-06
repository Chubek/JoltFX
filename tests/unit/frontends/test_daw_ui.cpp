#include "imgui.h"
#include "imgui_internal.h"
#include "jfx/desktop_frontend.h"
#include "jfx/jfx_vst3.h"
#include "jfx_test_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static void frame(jfx_desktop_frontend_t *f)
{
    assert(jfx_desktop_frontend_draw(f) == JFX_SUCCESS);
}
static void move(jfx_desktop_frontend_t *f, ImVec2 p)
{
    ImGui::GetIO().AddMousePosEvent(p.x, p.y);
    frame(f);
}
static void click(jfx_desktop_frontend_t *f, ImVec2 p)
{
    move(f, p);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    frame(f);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    frame(f);
    frame(f);
}
static void activate(jfx_desktop_frontend_t *f, ImGuiID id)
{
    ImGui::GetCurrentContext()->NavNextActivateId = id;
    frame(f);
    frame(f);
}
static ImGuiWindow *window(const char *part)
{
    for (auto *w : ImGui::GetCurrentContext()->Windows)
        if (w->Active && std::strstr(w->Name, part))
            return w;
    assert(false);
    return nullptr;
}
static ImVec2 locate(jfx_desktop_frontend_t *f, ImGuiWindow *w, ImGuiID id)
{
    move(f, ImVec2(0, 0));
    ImGui::DebugLocateItem(id);
    frame(f);
    ImRect bounds = w->InnerRect;
    bounds.Expand(4);
    ImVec2 lo(FLT_MAX, FLT_MAX), hi(-FLT_MAX, -FLT_MAX);
    for (const auto &v : ImGui::GetForegroundDrawList()->VtxBuffer)
        if (bounds.Contains(v.pos)) {
            lo.x = std::fmin(lo.x, v.pos.x);
            lo.y = std::fmin(lo.y, v.pos.y);
            hi.x = std::fmax(hi.x, v.pos.x);
            hi.y = std::fmax(hi.y, v.pos.y);
        }
    if (!(lo.x <= hi.x && lo.y <= hi.y))
        std::fprintf(stderr, "Unable to locate %u in %s (unresolved %u)\n", id, w->Name,
                     ImGui::GetCurrentContext()->DebugLocateId);
    assert(lo.x <= hi.x && lo.y <= hi.y);
    return ImVec2((lo.x + hi.x) / 2, (lo.y + hi.y) / 2);
}
static float peak(jfx_desktop_frontend_t *f)
{
    jfx_audio_mixer_t *m = nullptr;
    assert(jfx_desktop_frontend_audio_mixer(f, 8000, &m) == JFX_SUCCESS);
    float pcm[128];
    assert(jfx_audio_mixer_render(m, 200, 64, pcm, 128) == JFX_SUCCESS);
    jfx_audio_mixer_destroy(m);
    return pcm[0];
}
static void wave(const char *path)
{
    FILE *file = std::fopen(path, "wb");
    assert(file);
    auto le = [&](unsigned v, unsigned n) {
        for (unsigned i = 0; i < n; ++i)
            std::fputc((int) ((v >> (i * 8)) & 255), file);
    };
    std::fwrite("RIFF", 1, 4, file);
    le(36 + 16000, 4);
    std::fwrite("WAVEfmt ", 1, 8, file);
    le(16, 4);
    le(1, 2);
    le(1, 2);
    le(8000, 4);
    le(16000, 4);
    le(2, 2);
    le(16, 2);
    std::fwrite("data", 1, 4, file);
    le(16000, 4);
    for (int i = 0; i < 8000; ++i)
        le(8192, 2);
    assert(!std::fclose(file));
}
int main(int argc, char **argv)
{
    assert(argc == 3);
    wave(argv[2]);
    jfx_desktop_frontend_config_t config{};
    config.size = sizeof(config);
    config.width = 1280;
    config.height = 1000;
    config.backend_name = jfx_test_backend();
    jfx_desktop_frontend_t *f = nullptr;
    assert(jfx_desktop_frontend_create(&config, &f) == JFX_SUCCESS);
    ImGui::GetIO().IniFilename = nullptr;
    assert(jfx_desktop_frontend_edit(f, "sequence.new", 2, 2, 25, 1, "") == JFX_SUCCESS);
    assert(jfx_desktop_frontend_edit(f, "clip.add", 0, JFX_CLIP_AUDIO, 0, 25, argv[2]) ==
           JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_panel_visible(f, JFX_DESKTOP_PANEL_VIEWPORT, false) ==
           JFX_SUCCESS);
    assert(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_DAW) == JFX_SUCCESS);
    frame(f);
    frame(f);
    frame(f);
    frame(f);
    auto *editor = window("/Editor interface_");
    auto *bar = ImGui::GetCurrentContext()->TabBars.GetByKey(editor->GetID("DAW views"));
    assert(bar);
    bool found = false;
    for (auto &tab : bar->Tabs)
        if (!std::strcmp(ImGui::TabBarGetTabName(bar, &tab), "VST3 Inserts")) {
            ImVec2 p(bar->BarRect.Min.x + tab.Offset + tab.Width / 2 - bar->ScrollingAnim,
                     bar->BarRect.GetCenter().y);
            click(f, p);
            found = true;
            break;
        }
    assert(found);
    frame(f);
    frame(f);
    ImGuiID view_seed = bar->SelectedTabId;
    auto *selected = ImGui::TabBarFindTabByID(bar, view_seed);
    assert(selected);
    assert(!std::strcmp(ImGui::TabBarGetTabName(bar, selected), "VST3 Inserts"));
    auto id = [&](const char *label) { return ImHashStr(label, 0, view_seed); };
    /* Enter a real module path, scan it, and insert via actual browser buttons. */
    click(f, locate(f, editor, id("VST3 plugin or folder")));
    auto &io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    frame(f);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    io.AddInputCharactersUTF8(argv[1]);
    frame(f);
    io.AddKeyEvent(ImGuiKey_Enter, true);
    frame(f);
    io.AddKeyEvent(ImGuiKey_Enter, false);
    frame(f);
    activate(f, id("Scan path"));
    activate(f, id("Add VST3 insert"));
    char state[8192];
    assert(jfx_desktop_frontend_sequence_state(f, state, sizeof(state)) == JFX_SUCCESS);
    assert(std::strstr(state, "12345678112233445566778801020304"));
    assert(peak(f) == .25f);
    activate(f, id("Edit VST3 parameters"));
    auto *parameters = window("/VST3 parameters_");
    int zero = 0;
    ImGuiID seed = ImHashData(&zero, sizeof(zero), parameters->IDStack.back());
    ImVec2 knob = locate(f, parameters, ImHashStr("Gain", 0, seed));
    move(f, knob);
    io.AddMouseButtonEvent(0, true);
    frame(f);
    for (int i = 1; i <= 4; ++i)
        move(f, ImVec2(knob.x - 10 * (float) i, knob.y));
    io.AddMouseButtonEvent(0, false);
    frame(f);
    frame(f);
    float wet = peak(f);
    assert(wet > 0 && wet < .25f);
    assert(jfx_desktop_frontend_edit(f, "undo", 0, 0, 0, 0, "") == JFX_SUCCESS && peak(f) == .25f);
    assert(jfx_desktop_frontend_edit(f, "redo", 0, 0, 0, 0, "") == JFX_SUCCESS && peak(f) == wet);
    seed = ImHashData(&zero, sizeof(zero), view_seed);
    activate(f, ImHashStr("Enabled", 0, seed));
    assert(peak(f) == .25f);
    assert(jfx_desktop_frontend_edit(f, "undo", 0, 0, 0, 0, "") == JFX_SUCCESS && peak(f) == wet);
    activate(f,id("Edit VST3 parameters"));
    activate(f,id("Capture plugin state"));
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"stateBytes\":28"));
    assert(peak(f)==wet);
    assert(jfx_desktop_frontend_edit(f,"undo",0,0,0,0,"")==JFX_SUCCESS && peak(f)==wet);
    auto tab=[&](const char *name) {
        for (auto &item:bar->Tabs) if (!std::strcmp(ImGui::TabBarGetTabName(bar,&item),name)) {
            click(f,ImVec2(bar->BarRect.Min.x+item.Offset+item.Width/2-bar->ScrollingAnim,bar->BarRect.GetCenter().y));
            frame(f); view_seed=bar->SelectedTabId; return;
        }
        assert(false);
    };
    tab("MIDI / Instruments");
    activate(f,id("Add MIDI track")); activate(f,id("Add MIDI clip")); activate(f,id("Add note"));
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS);
    assert(std::strstr(state,"\"source\":\"midi\"") && std::strstr(state,"\"pitch\":60,\"channel\":0"));
    assert(jfx_desktop_frontend_edit(f,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,"\"pitch\":60"));
    tab("Automation"); activate(f,id("Add / update automation key"));
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && std::strstr(state,"\"audioAutomation\":[{\"target\":0"));
    assert(jfx_desktop_frontend_edit(f,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_desktop_frontend_sequence_state(f,state,sizeof(state))==JFX_SUCCESS && !std::strstr(state,"\"audioAutomation\":[{\"target\":0"));
    assert(jfx_desktop_frontend_set_panel_visible(f, JFX_DESKTOP_PANEL_DAW, false) == JFX_SUCCESS);
    assert(!jfx_desktop_frontend_panel_visible(f, JFX_DESKTOP_PANEL_DAW));
    assert(jfx_desktop_frontend_set_workspace(f, JFX_DESKTOP_WORKSPACE_DAW) == JFX_SUCCESS);
    frame(f);
    frame(f);
    jfx_desktop_frontend_destroy(f);
    assert(!std::remove(argv[2]));
    return 0;
}
