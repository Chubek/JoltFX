/* Direct Linux console host. No SDL window, EGL or OpenGL context is created.
 * libinput supplies pointer/keyboard events; xkbcommon supplies UTF-8 text.
 * VT release requests exit cleanly rather than rendering into another session. */
#include "novogfx_host.h"
#include "novogfx_display.h"
#include "software_ui.h"
#include "imgui.h"
#include <SDL.h>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>
#include <linux/input-event-codes.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <chrono>

namespace {
volatile sig_atomic_t stop_requested = 0;
volatile sig_atomic_t release_requested = 0;
constexpr int signals[] = { SIGINT, SIGTERM, SIGHUP, SIGUSR1, SIGUSR2 };
void stop_handler(int signal) { stop_requested = 1; if (signal == SIGUSR1) release_requested = 1; }
int open_input(const char *path, int flags, void *) { int fd = open(path, flags | O_CLOEXEC); return fd < 0 ? -errno : fd; }
void close_input(int fd, void *) { close(fd); }
const libinput_interface input_interface = { open_input, close_input };

ImGuiKey key_for(uint32_t code) {
    switch (code) {
#define KEYMAP(linux_key, imgui_key) case KEY_##linux_key: return ImGuiKey_##imgui_key
        KEYMAP(TAB, Tab); KEYMAP(LEFT, LeftArrow); KEYMAP(RIGHT, RightArrow);
        KEYMAP(UP, UpArrow); KEYMAP(DOWN, DownArrow); KEYMAP(PAGEUP, PageUp);
        KEYMAP(PAGEDOWN, PageDown); KEYMAP(HOME, Home); KEYMAP(END, End);
        KEYMAP(INSERT, Insert); KEYMAP(DELETE, Delete); KEYMAP(BACKSPACE, Backspace);
        KEYMAP(SPACE, Space); KEYMAP(ENTER, Enter); KEYMAP(ESC, Escape);
        KEYMAP(LEFTCTRL, LeftCtrl); KEYMAP(RIGHTCTRL, RightCtrl);
        KEYMAP(LEFTSHIFT, LeftShift); KEYMAP(RIGHTSHIFT, RightShift);
        KEYMAP(LEFTALT, LeftAlt); KEYMAP(RIGHTALT, RightAlt);
        KEYMAP(LEFTMETA, LeftSuper); KEYMAP(RIGHTMETA, RightSuper);
        KEYMAP(A,A); KEYMAP(B,B); KEYMAP(C,C); KEYMAP(D,D); KEYMAP(E,E);
        KEYMAP(F,F); KEYMAP(G,G); KEYMAP(H,H); KEYMAP(I,I); KEYMAP(J,J);
        KEYMAP(K,K); KEYMAP(L,L); KEYMAP(M,M); KEYMAP(N,N); KEYMAP(O,O);
        KEYMAP(P,P); KEYMAP(Q,Q); KEYMAP(R,R); KEYMAP(S,S); KEYMAP(T,T);
        KEYMAP(U,U); KEYMAP(V,V); KEYMAP(W,W); KEYMAP(X,X); KEYMAP(Y,Y); KEYMAP(Z,Z);
        KEYMAP(0,0); KEYMAP(1,1); KEYMAP(2,2); KEYMAP(3,3); KEYMAP(4,4);
        KEYMAP(5,5); KEYMAP(6,6); KEYMAP(7,7); KEYMAP(8,8); KEYMAP(9,9);
        KEYMAP(F1,F1); KEYMAP(F2,F2); KEYMAP(F3,F3); KEYMAP(F4,F4);
        KEYMAP(F5,F5); KEYMAP(F6,F6); KEYMAP(F7,F7); KEYMAP(F8,F8);
        KEYMAP(F9,F9); KEYMAP(F10,F10); KEYMAP(F11,F11); KEYMAP(F12,F12);
        KEYMAP(APOSTROPHE,Apostrophe); KEYMAP(COMMA,Comma); KEYMAP(MINUS,Minus);
        KEYMAP(DOT,Period); KEYMAP(SLASH,Slash); KEYMAP(SEMICOLON,Semicolon);
        KEYMAP(EQUAL,Equal); KEYMAP(LEFTBRACE,LeftBracket); KEYMAP(RIGHTBRACE,RightBracket);
        KEYMAP(BACKSLASH,Backslash); KEYMAP(GRAVE,GraveAccent);
        KEYMAP(CAPSLOCK,CapsLock); KEYMAP(NUMLOCK,NumLock); KEYMAP(SCROLLLOCK,ScrollLock);
        KEYMAP(KP0,Keypad0); KEYMAP(KP1,Keypad1); KEYMAP(KP2,Keypad2); KEYMAP(KP3,Keypad3);
        KEYMAP(KP4,Keypad4); KEYMAP(KP5,Keypad5); KEYMAP(KP6,Keypad6); KEYMAP(KP7,Keypad7);
        KEYMAP(KP8,Keypad8); KEYMAP(KP9,Keypad9); KEYMAP(KPDOT,KeypadDecimal);
        KEYMAP(KPSLASH,KeypadDivide); KEYMAP(KPASTERISK,KeypadMultiply); KEYMAP(KPMINUS,KeypadSubtract);
        KEYMAP(KPPLUS,KeypadAdd); KEYMAP(KPENTER,KeypadEnter); KEYMAP(KPEQUAL,KeypadEqual);
#undef KEYMAP
        default: return ImGuiKey_None;
    }
}
}

struct jfx_novogfx_host {
    jfx_software_ui ui;
    jfx_novogfx_display_t *display = nullptr;
    libinput *input = nullptr;
    xkb_context *xkb = nullptr;
    xkb_keymap *keymap = nullptr;
    xkb_state *keys = nullptr;
    int tty = -1, keyboard_mode = 0, display_mode = KD_TEXT;
    vt_mode vt{};
    termios terminal{};
    bool console_saved = false, vt_changed = false;
    jfx_result_t result = JFX_SUCCESS;
    size_t handlers = 0;
    struct sigaction saved_handlers[5]{};
    float mouse_x = 0, mouse_y = 0;
    uint32_t repeat = 0;
    double next_repeat = 0;
    std::chrono::steady_clock::time_point last = std::chrono::steady_clock::now();
};

void jfx_novogfx_host_destroy(jfx_novogfx_host *h) {
    if (!h) return;
    jfx_novogfx_display_destroy(h->display);
    if (h->input) libinput_unref(h->input);
    if (h->keys) xkb_state_unref(h->keys);
    if (h->keymap) xkb_keymap_unref(h->keymap);
    if (h->xkb) xkb_context_unref(h->xkb);
    if (h->console_saved) {
        if (ioctl(h->tty, KDSKBMODE, h->keyboard_mode) != 0)
            std::fprintf(stderr, "NovoGFX restore keyboard mode: errno %d\n", errno);
        if (ioctl(h->tty, KDSETMODE, h->display_mode) != 0)
            std::fprintf(stderr, "NovoGFX restore console mode: errno %d\n", errno);
        if (tcsetattr(h->tty, TCSANOW, &h->terminal) != 0)
            std::fprintf(stderr, "NovoGFX restore terminal: errno %d\n", errno);
    }
    if (h->vt_changed) {
        if (release_requested && ioctl(h->tty, VT_RELDISP, 1) != 0)
            std::fprintf(stderr, "NovoGFX release VT: errno %d\n", errno);
        if (ioctl(h->tty, VT_SETMODE, &h->vt) != 0)
            std::fprintf(stderr, "NovoGFX restore VT mode: errno %d\n", errno);
    }
    for (size_t i = 0; i < h->handlers; ++i) sigaction(signals[i], &h->saved_handlers[i], nullptr);
    if (h->tty >= 0) close(h->tty);
    ImGui::GetIO().MouseDrawCursor = false;
    ImGui::GetIO().BackendPlatformName = nullptr;
    tilly::destroy(h);
}

jfx_novogfx_host *jfx_novogfx_host_create(char *error, size_t size) {
    auto *h = tilly::create<jfx_novogfx_host>();
    if (!h) return nullptr;
    const char *reason = "NovoGFX requires an active Linux virtual console (not an X11/Wayland terminal)";
    /* Do not allow an SSH/PTY launch to modeset someone else's active seat. */
    h->tty = open("/dev/tty", O_RDWR | O_CLOEXEC);
    struct stat st{}; vt_stat active{};
    if (h->tty < 0 || fstat(STDIN_FILENO, &st) != 0 || major(st.st_rdev) != 4 ||
        minor(st.st_rdev) == 0 || minor(st.st_rdev) > 63 ||
        ioctl(h->tty, VT_GETSTATE, &active) != 0 || active.v_active != minor(st.st_rdev) ||
        ioctl(h->tty, VT_GETMODE, &h->vt) != 0 || h->vt.mode != VT_AUTO ||
        ioctl(h->tty, KDGKBMODE, &h->keyboard_mode) != 0 ||
        ioctl(h->tty, KDGETMODE, &h->display_mode) != 0 || h->display_mode != KD_TEXT ||
        tcgetattr(h->tty, &h->terminal) != 0) goto failed;
    h->console_saved = true;
    stop_requested = release_requested = 0;
    for (int signal : signals) {
        struct sigaction action{}; action.sa_handler = stop_handler; sigemptyset(&action.sa_mask);
        if (sigaction(signal, &action, &h->saved_handlers[h->handlers]) != 0) goto failed;
        ++h->handlers;
    }
    {
        vt_mode mode = h->vt; mode.mode = VT_PROCESS; mode.relsig = SIGUSR1; mode.acqsig = SIGUSR2;
        reason = "NovoGFX could not take ownership of the active VT";
        if (ioctl(h->tty, VT_SETMODE, &mode) != 0) goto failed;
        h->vt_changed = true;
        termios raw = h->terminal; cfmakeraw(&raw);
        if (tcsetattr(h->tty, TCSANOW, &raw) != 0 || ioctl(h->tty, KDSKBMODE, K_OFF) != 0 ||
            ioctl(h->tty, KDSETMODE, KD_GRAPHICS) != 0) goto failed;
    }
    reason = "NovoGFX input requires readable /dev/input/eventN devices and an XKB keymap";
    h->xkb = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (!h->xkb) goto failed;
    h->keymap = xkb_keymap_new_from_names(h->xkb, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (!h->keymap) goto failed;
    h->keys = xkb_state_new(h->keymap);
    if (!h->keys) goto failed;
    h->input = libinput_path_create_context(&input_interface, nullptr);
    if (!h->input) goto failed;
    {
        bool keyboard = false, pointer = false;
        for (unsigned i = 0; i < 128; ++i) {
            char path[64]; std::snprintf(path, sizeof(path), "/dev/input/event%u", i);
            if (access(path, R_OK) != 0) continue;
            auto *device = libinput_path_add_device(h->input, path);
            if (!device) continue;
            keyboard |= libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_KEYBOARD) != 0;
            pointer |= libinput_device_has_capability(device, LIBINPUT_DEVICE_CAP_POINTER) != 0;
        }
        if (!keyboard || !pointer) goto failed;
    }
    if (jfx_novogfx_display_create(std::getenv("JFX_NOVOGFX_DEVICE"), &h->display, error, size) != JFX_SUCCESS) {
        jfx_novogfx_host_destroy(h); return nullptr;
    }
    reason = "NovoGFX software ImGui renderer initialization failed";
    if (!h->ui.init(jfx_novogfx_display_width(h->display), jfx_novogfx_display_height(h->display))) goto failed;
    ImGui::GetIO().BackendPlatformName = "joltfx_novogfx_libinput";
    ImGui::GetIO().MouseDrawCursor = true;
    return h;
failed:
    if (error && size) std::snprintf(error, size, "%s", reason);
    jfx_novogfx_host_destroy(h);
    return nullptr;
}

static void text_input(jfx_novogfx_host *h, uint32_t code) {
    auto &io = ImGui::GetIO();
    if (xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0 ||
        xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0) return;
    char text[64]{};
    int n = xkb_state_key_get_utf8(h->keys, code + 8, text, sizeof(text));
    if (n > 0 && n < int(sizeof(text)) && static_cast<unsigned char>(text[0]) >= 32) io.AddInputCharactersUTF8(text);
}

bool jfx_novogfx_host_begin(jfx_novogfx_host *h) {
    auto &io = ImGui::GetIO();
    auto now = std::chrono::steady_clock::now();
    io.DeltaTime = std::clamp(float(std::chrono::duration<double>(now-h->last).count()), 0.001f, 0.25f);
    h->last = now;
    double seconds = std::chrono::duration<double>(now.time_since_epoch()).count();
    bool open = !stop_requested;
    int dispatched = libinput_dispatch(h->input);
    if (dispatched != 0) {
        std::fprintf(stderr, "NovoGFX libinput dispatch failed: %d\n", dispatched);
        h->result = JFX_ERROR_BACKEND_FAILURE;
        open = false;
    }
    while (auto *event = libinput_get_event(h->input)) {
        auto type = libinput_event_get_type(event);
        if (type == LIBINPUT_EVENT_KEYBOARD_KEY) {
            auto *key = libinput_event_get_keyboard_event(event);
            uint32_t code = libinput_event_keyboard_get_key(key);
            bool down = libinput_event_keyboard_get_key_state(key) == LIBINPUT_KEY_STATE_PRESSED;
            /* Seat counts handle the same key held on multiple keyboards. */
            uint32_t count = libinput_event_keyboard_get_seat_key_count(key);
            if ((down && count == 1) || (!down && count == 0)) {
                xkb_state_update_key(h->keys, code + 8, down ? XKB_KEY_DOWN : XKB_KEY_UP);
                ImGuiKey mapped = key_for(code);
                if (mapped != ImGuiKey_None) io.AddKeyEvent(mapped, down);
                io.AddKeyEvent(ImGuiMod_Ctrl, xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0);
                io.AddKeyEvent(ImGuiMod_Shift, xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0);
                io.AddKeyEvent(ImGuiMod_Alt, xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0);
                io.AddKeyEvent(ImGuiMod_Super, xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_LOGO, XKB_STATE_MODS_EFFECTIVE) > 0);
                if (down) {
                    text_input(h, code);
                    if (xkb_keymap_key_repeats(h->keymap, code + 8)) { h->repeat = code; h->next_repeat = seconds + 0.4; }
                    if (code == KEY_ESC &&
                        xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0 &&
                        xkb_state_mod_name_is_active(h->keys, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0) open = false;
                } else if (h->repeat == code) h->repeat = 0;
            }
        } else if (type == LIBINPUT_EVENT_POINTER_MOTION || type == LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE) {
            auto *p = libinput_event_get_pointer_event(event);
            if (type == LIBINPUT_EVENT_POINTER_MOTION) {
                h->mouse_x += float(libinput_event_pointer_get_dx(p)); h->mouse_y += float(libinput_event_pointer_get_dy(p));
            } else {
                h->mouse_x = float(libinput_event_pointer_get_absolute_x_transformed(p, h->ui.width));
                h->mouse_y = float(libinput_event_pointer_get_absolute_y_transformed(p, h->ui.height));
            }
            h->mouse_x = std::clamp(h->mouse_x, 0.0f, float(h->ui.width-1));
            h->mouse_y = std::clamp(h->mouse_y, 0.0f, float(h->ui.height-1));
            io.AddMousePosEvent(h->mouse_x, h->mouse_y);
        } else if (type == LIBINPUT_EVENT_POINTER_BUTTON) {
            auto *p = libinput_event_get_pointer_event(event);
            uint32_t button = libinput_event_pointer_get_button(p);
            int b = button == BTN_LEFT ? 0 : button == BTN_RIGHT ? 1 : button == BTN_MIDDLE ? 2 : button == BTN_SIDE ? 3 : button == BTN_EXTRA ? 4 : -1;
            if (b >= 0) io.AddMouseButtonEvent(b, libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED);
        } else if (type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL || type == LIBINPUT_EVENT_POINTER_SCROLL_FINGER || type == LIBINPUT_EVENT_POINTER_SCROLL_CONTINUOUS) {
            auto *p = libinput_event_get_pointer_event(event);
            float scroll[2]{};
            libinput_pointer_axis axes[] = { LIBINPUT_POINTER_AXIS_SCROLL_HORIZONTAL, LIBINPUT_POINTER_AXIS_SCROLL_VERTICAL };
            for (int i=0; i<2; ++i) if (libinput_event_pointer_has_axis(p, axes[i]))
                scroll[i] = float(type == LIBINPUT_EVENT_POINTER_SCROLL_WHEEL ? libinput_event_pointer_get_scroll_value_v120(p, axes[i])/120.0 : libinput_event_pointer_get_scroll_value(p, axes[i])/15.0);
            io.AddMouseWheelEvent(-scroll[0], -scroll[1]);
        } else if (type == LIBINPUT_EVENT_DEVICE_REMOVED) {
            io.ClearInputKeys(); io.ClearInputMouse(); h->repeat = 0;
            xkb_state_unref(h->keys); h->keys = xkb_state_new(h->keymap);
            if (!h->keys) {
                std::fprintf(stderr, "NovoGFX could not rebuild the XKB keyboard state\n");
                h->result = JFX_ERROR_OUT_OF_MEMORY;
                open = false;
            }
        }
        libinput_event_destroy(event);
        if (!h->keys) break;
    }
    if (h->keys && h->repeat && seconds >= h->next_repeat) { text_input(h, h->repeat); h->next_repeat = seconds + 0.035; }
    io.DisplaySize = ImVec2(float(h->ui.width), float(h->ui.height));
    io.DisplayFramebufferScale = ImVec2(1,1);
    ImGui::NewFrame(); // Balanced even when stop/VT release was requested.
    return open;
}

jfx_result_t jfx_novogfx_host_end(jfx_novogfx_host *h) {
    ImGui::Render();
    if (!h->ui.render(ImGui::GetDrawData())) { std::fprintf(stderr, "NovoGFX UI rasterization: %s\n", SDL_GetError()); return JFX_ERROR_BACKEND_FAILURE; }
    auto result = jfx_novogfx_display_present(h->display, h->ui.pixels.data(), size_t(h->ui.width)*4);
    SDL_Delay(1); // Never spin at unbounded speed while a flip is pending.
    return result == JFX_ERROR_BUSY ? JFX_SUCCESS : result;
}
uint32_t jfx_novogfx_host_width(const jfx_novogfx_host *h) { return h->ui.width; }
jfx_result_t jfx_novogfx_host_result(const jfx_novogfx_host *h) { return h->result; }
uint32_t jfx_novogfx_host_height(const jfx_novogfx_host *h) { return h->ui.height; }
void *jfx_novogfx_host_upload(jfx_novogfx_host *h, uint32_t w, uint32_t height, const uint8_t *rgba) { return h->ui.upload(w, height, rgba); }
