#ifndef JFX_PAGE_DESIGN_WORKSPACE_H
#define JFX_PAGE_DESIGN_WORKSPACE_H
#include "jolttype/project.hpp"
#include "host_window.h"
struct jfx_desktop_frontend;

namespace jfx::page {
struct FileJob;
struct PreviewJob;
class Workspace {
public:
    Workspace();
    ~Workspace();
    jolttype::Session session;
    jolttype::Typelet project;
    bool files_requested = false;
    void draw(jfx_desktop_window_t *window=nullptr);
    void tick(double now, int interval, bool busy);
    void deactivate();
    bool commit(double now);
    bool history(bool redo);
    bool can_history(bool redo) const { return !busy() && !text_pending && session.can_history(redo); }
    bool load(const char *path);
    bool save(const char *path);
    bool reset();
    bool dirty() const { return text_pending || session.dirty(); }
    bool busy() const { return file_job != nullptr; }
    bool background_busy() const {return file_job || preview_job;}
    const jolttype::Bitmap &pdf_bitmap() const {return bitmap;}
    const tilly::string &pdf_bytes() const {return pdf;}
private:
    FileJob *file_job = nullptr;
    PreviewJob *preview_job = nullptr;
    jolttype::Hatch hatch;
    jolttype::Bitmap bitmap;
    tilly::string pdf;
    uint64_t revision=1,pdf_revision=0;
    int pdf_page=-1;
    bool preview_requested=false,show_progress=false,ide_mode=false;
    void *pdf_texture=nullptr;
    char pdf_message[256]{};
    char output_path[1024]="page.pdf",package_path[1024]="page.typelet";
    int output_target=0;
    char asset_path[1024]{},asset_id[64]{},asset_tags[1024]{},asset_filter[128]{};
    char asset_dependencies[1024]{};
    char build_input[64]="main",build_output[64]="layout.pdf";
    int build_target=0;
    int asset_kind=0;
    char inventory_selection[64]{};
    char asset_editor[jolttype::max_source_bytes+1]{};
    char manifest_editor[jolttype::max_source_bytes+1]{};
    char package_name[129]="untitled",package_version[64]="1.0.0",package_author[257]{},package_description[1025]{};
    double module_arguments[64][16]{};
    void inventory_panel();
    void package_panel();
    void module_panel();
    void select_asset(const jolttype::Asset *);
    void inventory_action(int action);
    void preview_tick();
    void progress_display(jfx_desktop_window_t *);
    void published();
    bool sync_project();
    char source[jolttype::max_source_bytes + 1]{};
    char text[jolttype::max_source_bytes + 1]{};
    char path[1024] = "page.jtype", export_path[1024] = "page.svg";
    char selected[64]{};
    char message[256]{};
    unsigned source_id = 0, text_id = 0;
    int page = 0;
    float zoom = .65f;
    bool text_pending = false, dragging = false, was_dirty = false;
    double dirty_since = 0, drag_x = 0, drag_y = 0;
    float mouse_x = 0, mouse_y = 0;
    void sync();
    void select(const jolttype::Element *element);
    void append(const char *kind);
    void canvas();
    void files();
    void queue_file(int action);
    void poll_file();
};
// Private integration/test access, not part of the installed C ABI.
Workspace *workspace(jfx_desktop_frontend *frontend);
}
#endif
