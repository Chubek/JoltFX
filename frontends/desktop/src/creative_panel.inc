/* Creative-programming tab: code pad + result view.
 *
 * Included in the UI namespace of desktop_frontend.cpp. Headless logic
 * (compile/render/export) lives next to it as static helpers with the public
 * `jfx_desktop_frontend_creative_*` C API; this file only composes widgets.
 * Syntax coloring is a small hand-rolled tokenizer (parens/keywords/numbers/
 * comments); novoparse stays out of the build per the agreed scope. */

namespace creative_highlight {
enum class Kind { Plain, Paren, Keyword, Number, Comment, String };
struct Span {
    Kind kind;
    size_t begin, end;
};

inline bool is_op_char(char c) {
    return c == '+' || c == '-' || c == '*' || c == '/' || c == '<' || c == '>' ||
           c == '=' || c == '!' || c == '&' || c == '|';
}

// Splits one line into colored spans. Comments (`;` to end of line) win over
// everything; numbers are finite f32 literals; keywords are the defkernel
// form, `rgba`/`select`, and the JBC1 operation names both compilers accept.
inline void tokenize_line(const char *begin, const char *end, Span *out, size_t capacity,
    size_t *out_count) {
    size_t count = 0;
    auto push = [&](Kind kind, size_t b, size_t e) {
        if (count < capacity && e > b) {
            out[count++] = Span{kind, b, e};
        }
    };
    static const char *keywords[] = {"defkernel", "rgba", "select", "min", "max", "abs",
        "floor", "pow", "sqrt", "and", "or", "not", "bitwise-and", "bitwise-or",
        "bitwise-xor", "shl", "shr"};
    const char *cursor = begin;
    size_t base = 0;
    while (cursor < end) {
        char c = *cursor;
        if (c == ';') {
            push(Kind::Comment, base + (size_t)(cursor - begin),
                base + (size_t)(end - begin));
            break;
        }
        if (c == ' ' || c == '\t' || c == '\r') {
            ++cursor;
            continue;
        }
        if (c == '(' || c == ')' || c == '[' || c == ']') {
            size_t at = (size_t)(cursor - begin);
            push(Kind::Paren, base + at, base + at + 1);
            ++cursor;
            continue;
        }
        const char *start = cursor;
        while (cursor < end && *cursor != ' ' && *cursor != '\t' && *cursor != '\r' &&
               *cursor != '(' && *cursor != ')' && *cursor != '[' && *cursor != ']' &&
               *cursor != ';') {
            ++cursor;
        }
        size_t at = (size_t)(start - begin);
        size_t len = (size_t)(cursor - start);
        char word[64];
        size_t copy = len < sizeof(word) - 1 ? len : sizeof(word) - 1;
        memcpy(word, start, copy);
        word[copy] = 0;
        bool keyword = false;
        // Single-char operators highlight as keywords too.
        if (len == 1 && is_op_char(word[0])) {
            keyword = true;
        } else {
            for (const char *k : keywords) {
                if (!strcmp(word, k)) {
                    keyword = true;
                    break;
                }
            }
        }
        char *stop = nullptr;
        strtof(word, &stop);
        bool number = (stop && *stop == 0);
        push(keyword ? Kind::Keyword : number ? Kind::Number : Kind::Plain, base + at,
            base + at + len);
    }
    *out_count = count;
}

inline ImVec4 color_for(Kind kind) {
    switch (kind) {
    case Kind::Paren:
        return ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
    case Kind::Keyword:
        return ImVec4(0.55f, 0.85f, 1.0f, 1.0f);
    case Kind::Number:
        return ImVec4(0.55f, 1.0f, 0.75f, 1.0f);
    case Kind::Comment:
        return ImVec4(0.55f, 0.6f, 0.68f, 1.0f);
    case Kind::String:
        return ImVec4(1.0f, 0.7f, 0.55f, 1.0f);
    default:
        return ImVec4(0.88f, 0.88f, 0.88f, 1.0f);
    }
}
} // namespace creative_highlight

void creative_highlight_preview(const char *source) {
    if (!source || !*source) {
        ImGui::TextDisabled("Nothing to highlight yet.");
        return;
    }
    // Read-only colored echo of the first 40 lines; editing stays in the
    // InputTextMultiline below (ImGui inputs cannot color ranges).
    ImGui::BeginChild("Creative highlight", ImVec2(0, 160), true);
    const char *cursor = source;
    for (int line = 0; line < 40 && cursor && *cursor; ++line) {
        const char *end = strchr(cursor, '\n');
        const char *line_end = end ? end : cursor + strlen(cursor);
        creative_highlight::Span spans[64];
        size_t count = 0;
        creative_highlight::tokenize_line(cursor, line_end, spans, 64, &count);
        for (size_t i = 0; i < count; ++i) {
            if (i) {
                ImGui::SameLine(0, 0);
            }
            char text[256];
            size_t len = spans[i].end - spans[i].begin;
            // spans are relative to the line start.
            if (len >= sizeof(text)) {
                len = sizeof(text) - 1;
            }
            memcpy(text, cursor + spans[i].begin, len);
            text[len] = 0;
            // Pad gaps with the plain color so columns roughly line up.
            ImGui::TextColored(creative_highlight::color_for(spans[i].kind), "%s", text);
            // Keep a space between tokens (SameLine(0,0) removes it).
            ImGui::SameLine(0, 4);
        }
        ImGui::NewLine();
        cursor = end ? end + 1 : nullptr;
    }
    ImGui::EndChild();
}

void creative_panel(jfx_desktop_frontend_t *f) {
    ImGui::TextUnformatted("Zoltan creative sketch: one (defkernel [x y t ...] body) program.");
    ImGui::TextDisabled("x/y are normalized coords, t is seconds, mx/my the pointer. Same JBC1 both compilers emit.");
    ImGui::Checkbox("Show code", &f->creative_show_code);
    ImGui::SameLine();
    ImGui::Checkbox("Show preview", &f->creative_show_preview);
    ImGui::SameLine();
    if (ImGui::Button("Run")) {
        f->creative_dirty = true;
        f->creative_status = jfx_desktop_frontend_creative_render(f, (uint32_t)f->creative_width,
            (uint32_t)f->creative_height, nullptr, 0);
        if (f->creative_status != JFX_SUCCESS) {
            std::snprintf(f->status, sizeof(f->status), "%s",
                jfx_desktop_frontend_creative_error(f));
        } else {
            std::snprintf(f->status, sizeof(f->status), "creative sketch ok");
        }
        f->preview_dirty = true;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Animate", &f->creative_playing);
    if (f->creative_error[0]) {
        ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.45f, 1.0f), "%s", f->creative_error);
    }

    bool wide = ImGui::GetContentRegionAvail().x >= 760;
    bool both = f->creative_show_code && f->creative_show_preview;
    bool columns = both && wide && ImGui::BeginTable("Creative layout", 2, ImGuiTableFlags_Resizable);
    if (columns) {
        ImGui::TableSetupColumn("Code", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Result", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableNextColumn();
    }
    if (f->creative_show_code) {
        if (ImGui::BeginChild("Creative code", ImVec2(0, 0))) {
            if (ImGui::CollapsingHeader("Syntax colors", ImGuiTreeNodeFlags_DefaultOpen)) {
                creative_highlight_preview(f->creative_source);
            }
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputTextMultiline("##Creative source", f->creative_source,
                    sizeof(f->creative_source), ImVec2(-1, 220))) {
                f->creative_error[0] = 0;
                f->creative_dirty = true;
            }
            ImGui::TextDisabled("Ops: + - * / min max abs floor pow sqrt < > <= >= = != and or not bitwise-and/or/xor shl shr select");
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("t", &f->creative_t, 0.0f, 60.0f)) {
                f->creative_dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("mx", &f->creative_mx, 0.0f, 1.0f)) {
                f->creative_dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120);
            if (ImGui::SliderFloat("my", &f->creative_my, 0.0f, 1.0f)) {
                f->creative_dirty = true;
            }
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("Canvas W", &f->creative_width)) {
                f->creative_dirty = true;
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100);
            if (ImGui::InputInt("Canvas H", &f->creative_height)) {
                f->creative_dirty = true;
            }
            if (f->creative_width < 1) {
                f->creative_width = 1;
            }
            if (f->creative_width > 320) {
                f->creative_width = 320;
            }
            if (f->creative_height < 1) {
                f->creative_height = 1;
            }
            if (f->creative_height > 180) {
                f->creative_height = 180;
            }
            ImGui::InputText("Export path", f->creative_export_path, sizeof(f->creative_export_path));
            if (ImGui::Button("Export object (.o)")) {
                auto r = jfx_desktop_frontend_creative_export(f, "obj", f->creative_export_path);
                std::snprintf(f->status, sizeof(f->status), "%s",
                    r == JFX_SUCCESS ? "object exported" : jfx_desktop_frontend_creative_error(f));
            }
            ImGui::SameLine();
            if (ImGui::Button("Export HTML")) {
                auto r = jfx_desktop_frontend_creative_export(f, "html", f->creative_export_path);
                std::snprintf(f->status, sizeof(f->status), "%s",
                    r == JFX_SUCCESS ? "HTML exported" : jfx_desktop_frontend_creative_error(f));
            }
            ImGui::SameLine();
            if (ImGui::Button("Export WASM (WAT)")) {
                auto r = jfx_desktop_frontend_creative_export(f, "wasm", f->creative_export_path);
                std::snprintf(f->status, sizeof(f->status), "%s",
                    r == JFX_SUCCESS ? "WAT exported" : jfx_desktop_frontend_creative_error(f));
            }
            ImGui::TextWrapped("%s", f->status);
        }
        ImGui::EndChild();
    }
    if (columns) {
        ImGui::TableNextColumn();
    }
    if (f->creative_show_preview) {
        if (ImGui::BeginChild("Creative result", ImVec2(0, 0))) {
            ImGui::TextUnformatted("Result");
            // Render on demand into the shared preview buffer size; the
            // creative raster itself is bounded to 320x180.
            uint32_t w = (uint32_t)f->creative_width;
            uint32_t h = (uint32_t)f->creative_height;
            jfx_result_t preview_status = JFX_SUCCESS;
            if (!w || !h || w > 320 || h > 180) {
                ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "Canvas must be 1..320 x 1..180.");
                preview_status = JFX_ERROR_INVALID_ARGUMENT;
            } else {
                // Refresh the cached raster only when inputs changed or while
                // animating; the buffer is reused across frames otherwise.
                if (f->creative_dirty || f->creative_playing) {
                    preview_status = jfx_desktop_frontend_creative_render(f, w, h,
                        f->preview_rgba, (size_t)320 * 180 * 4u);
                }
                if (preview_status != JFX_SUCCESS) {
                    ImGui::TextColored(ImVec4(1, 0.45f, 0.45f, 1), "%s",
                        jfx_desktop_frontend_creative_error(f));
                }
            }
            if (preview_status == JFX_SUCCESS) {
                if (f->window) {
                    void *texture = jfx_desktop_window_upload_rgba8(f->window, w, h, f->preview_rgba);
                    if (texture) {
                        float width = std::fmin((float)w, ImGui::GetContentRegionAvail().x);
                        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<uintptr_t>(texture)),
                            ImVec2(width, width * (float)h / (float)(w ? w : 1)));
                    } else {
                        ImGui::TextDisabled("Preview texture upload failed.");
                    }
                } else {
                    ImGui::TextWrapped("Headless: %ux%u sketch rendered through the Glue VM.", w, h);
                }
            }
        }
        ImGui::EndChild();
    }
    if (columns) {
        ImGui::EndTable();
    }
    if (!f->creative_show_code && !f->creative_show_preview) {
        ImGui::TextDisabled("Both views are hidden; re-enable Show code / Show preview.");
    }
}
