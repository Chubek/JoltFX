#ifndef JFX_DESKTOP_ANIMATION_SCRIPT_H
#define JFX_DESKTOP_ANIMATION_SCRIPT_H
#include "animation_drawing.h"
#ifdef JFX_DESKTOP_HAVE_INGAGI
#include "../../../ingagi/animation/native.h"
#endif

namespace jfx_drawing {
inline bool bake_ingagi(Editor &e,char *error,size_t capacity) noexcept {
#ifdef JFX_DESKTOP_HAVE_INGAGI
    auto output=[](void *user,const char *data,size_t size)->int {
        return static_cast<Editor *>(user)->load_data(data,size)?0:1;
    };
    return ig_animation_bake(e.script,std::strlen(e.script),e.doc.fps,e.doc.frames,0,output,&e,error,capacity)==0;
#else
    (void)e;std::snprintf(error,capacity,"Enable JFX_INGAGI to bake animation scripts");return false;
#endif
}
inline bool export_canvas(const Editor &e,char *error,size_t capacity) noexcept try {
#ifdef JFX_DESKTOP_HAVE_INGAGI
    auto data=e.serialize();
    auto output=[](void *user,const char *bytes,size_t size)->int {
        return Editor::write_file(static_cast<const char *>(user),bytes,size)?0:1;
    };
    return ig_animation_export(data.data(),data.size(),1,output,const_cast<char *>(e.html_path),error,capacity)==0;
#else
    (void)e;std::snprintf(error,capacity,"Enable JFX_INGAGI for HTML5 Canvas export");return false;
#endif
} catch (...) {std::snprintf(error,capacity,"Animation export allocation failed");return false;}
}
#endif
