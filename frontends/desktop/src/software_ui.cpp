#include "software_ui.h"
#include <SDL.h>
#include "imgui.h"
#include "backends/imgui_impl_sdlrenderer2.h"

jfx_software_ui::~jfx_software_ui() {
    if (ready) ImGui_ImplSDLRenderer2_Shutdown();
    for (auto &slot : textures) if (slot.texture) SDL_DestroyTexture(slot.texture);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (surface) SDL_FreeSurface(surface);
}

bool jfx_software_ui::init(uint32_t w, uint32_t h) {
    if (surface || !w || !h || w > 8192 || h > 8192) return false;
    try { pixels.resize(size_t(w) * h * 4); } catch (...) { return false; }
    width = w; height = h;
    surface = SDL_CreateRGBSurfaceWithFormatFrom(pixels.data(), int(w), int(h), 32,
        int(w * 4), SDL_PIXELFORMAT_RGBA32);
    if (!surface) return false;
    renderer = SDL_CreateSoftwareRenderer(surface);
    if (!renderer) return false;
    ready = ImGui_ImplSDLRenderer2_Init(renderer);
    return ready;
}

bool jfx_software_ui::render(ImDrawData *data) {
    if (!ready || !data) return false;
    SDL_ClearError();
    SDL_SetRenderDrawColor(renderer, 15, 18, 23, 255);
    if (SDL_RenderClear(renderer) != 0) return false;
    ImGui_ImplSDLRenderer2_RenderDrawData(data, renderer);
    SDL_RenderPresent(renderer); // Flush queued CPU geometry into the surface.
    next_upload = 0; // All image handles in this frame have now been consumed.
    return SDL_GetError()[0] == '\0';
}

void *jfx_software_ui::upload(uint32_t w, uint32_t h, const uint8_t *rgba) {
    if (!ready || !rgba || !w || !h || w > 8192 || h > 8192) return nullptr;
    // Multiple previews in one frame must not overwrite/destroy a texture that
    // earlier draw commands still borrow. Reuse slots only after rasterization.
    if (next_upload >= 32) return nullptr;
    if (next_upload == textures.size()) {
        try { textures.emplace_back(); } catch (...) { return nullptr; }
    }
    auto &slot = textures[next_upload];
    if (!slot.texture || w != slot.width || h != slot.height) {
        SDL_Texture *next = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STATIC, int(w), int(h));
        if (!next) return nullptr;
        if (SDL_UpdateTexture(next, nullptr, rgba, int(w*4)) != 0) { SDL_DestroyTexture(next); return nullptr; }
        SDL_SetTextureBlendMode(next, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(next, SDL_ScaleModeNearest);
        if (slot.texture) SDL_DestroyTexture(slot.texture);
        slot.texture = next; slot.width = w; slot.height = h;
    } else if (SDL_UpdateTexture(slot.texture, nullptr, rgba, int(w*4)) != 0) return nullptr;
    ++next_upload;
    return slot.texture;
}
