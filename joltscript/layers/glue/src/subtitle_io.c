#include "joltscript/subtitle_io.h"
#include "tilly/memory.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#if defined(JFX_HAVE_LIBASS)
#include <ass/ass.h>
#endif
int jolt_subtitle_available(void) {
#if defined(JFX_HAVE_LIBASS)
    return 1;
#else
    return 0;
#endif
}
jolt_status_t jolt_subtitle_render(const char *path,double seconds,uint32_t w,uint32_t h,uint8_t *rgba) {
    if (!path || !*path || !rgba || !w || !h || w>4096 || h>4096 || !isfinite(seconds) || seconds<0 || seconds>1.e9) return JOLT_ERR_ARGUMENT;
#if defined(JFX_HAVE_LIBASS)
    FILE *file=fopen(path,"rb"); if (!file) return JOLT_ERR_ARGUMENT;
    if (fseek(file,0,SEEK_END)) { fclose(file); return JOLT_ERR_ARGUMENT; }
    long size=ftell(file);
    if (size<=0 || size>8*1024*1024 || fseek(file,0,SEEK_SET)) { fclose(file); return JOLT_ERR_ARGUMENT; }
    char *text=tilly_mem_alloc((size_t)size+1);
    if (!text) { fclose(file); return JOLT_ERR_MEMORY; }
    size_t read=fread(text,1,(size_t)size,file); fclose(file); text[read]=0;
    if (read!=(size_t)size) { tilly_mem_free(text); return JOLT_ERR_ARGUMENT; }
    ASS_Library *library=ass_library_init();
    if (!library) { tilly_mem_free(text); return JOLT_ERR_MEMORY; }
    ASS_Renderer *renderer=ass_renderer_init(library);
    ASS_Track *track=ass_read_memory(library,text,(size_t)size,NULL); tilly_mem_free(text);
    if (!renderer || !track) {
        if (renderer) ass_renderer_done(renderer);
        if (track) ass_free_track(track);
        ass_library_done(library); return JOLT_ERR_ARGUMENT;
    }
    ass_set_frame_size(renderer,(int)w,(int)h);
    ass_set_fonts(renderer,NULL,"sans-serif",ASS_FONTPROVIDER_AUTODETECT,NULL,1);
    int change=0;
    for (ASS_Image *image=ass_render_frame(renderer,track,(long long)llround(seconds*1000),&change);image;image=image->next) {
        unsigned color=image->color;
        for (int y=0;y<image->h;++y) for (int x=0;x<image->w;++x) {
            int dx=image->dst_x+x,dy=image->dst_y+y;
            if (dx<0 || dy<0 || dx>=(int)w || dy>=(int)h) continue;
            double a=(double)image->bitmap[y*image->stride+x]/255*(1-(double)(color&255)/255);
            uint8_t *p=rgba+((size_t)dy*w+(unsigned)dx)*4;
            double base=(double)p[3]/255,alpha=a+base*(1-a);
            for (unsigned c=0;c<3;++c) {
                double v=(double)((color>>(24-c*8))&255);
                p[c]=(uint8_t)lrint(alpha>0?(v*a+p[c]*base*(1-a))/alpha:0);
            }
            p[3]=(uint8_t)lrint(alpha*255);
        }
    }
    ass_free_track(track); ass_renderer_done(renderer); ass_library_done(library);
    return JOLT_OK;
#else
    return JOLT_ERR_CAPABILITY;
#endif
}
