#ifndef JFX_DESKTOP_SOFTWARE_UI_H
#define JFX_DESKTOP_SOFTWARE_UI_H
#include <cstdint>
#include "tilly/memory.hpp"
struct SDL_Surface;
struct SDL_Renderer;
struct SDL_Texture;
struct ImDrawData;

/* CPU-only ImGui target; SDL never creates a window or graphics context. */
struct jfx_software_ui {
    jfx_software_ui() = default;
    jfx_software_ui(const jfx_software_ui &) = delete;
    jfx_software_ui &operator=(const jfx_software_ui &) = delete;
    tilly::vector<uint8_t> pixels;
    SDL_Surface *surface = nullptr;
    SDL_Renderer *renderer = nullptr;
    struct texture_slot { SDL_Texture *texture = nullptr; uint32_t width = 0, height = 0; };
    tilly::vector<texture_slot> textures;
    size_t next_upload = 0;
    uint32_t width = 0, height = 0;
    bool ready = false;
    ~jfx_software_ui();
    bool init(uint32_t w, uint32_t h);
    bool render(ImDrawData *data);
    void *upload(uint32_t w, uint32_t h, const uint8_t *rgba);
};
#endif
