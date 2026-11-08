#include "jfx/jfx_nle.h"
#include "jfx/jfx_vst3.h"
#include "jfx/jfx_automation.h"
#include "opentime/rationalTime.h"
#include "tilly/memory.hpp"
#include "tilly/memory.h"
#include "yyjson.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {
void *alloc(void *,size_t n) { return tilly_mem_alloc(n); }
void *realloc_mem(void *,void *p,size_t,size_t n) { return tilly_mem_realloc(p,n); }
void free_mem(void *,void *p) { tilly_mem_free(p); }
const yyjson_alc allocator={alloc,realloc_mem,free_mem,nullptr};
struct Json { yyjson_doc *p=nullptr; ~Json() { yyjson_doc_free(p); } };
struct Timeline { jfx_timeline_t *p=nullptr; ~Timeline() { jfx_timeline_destroy(p); } };
using Text=tilly::string;
void quote(Text &s,const char *text) {
    s+='"';
    for (const unsigned char *p=(const unsigned char *)(text?text:"");*p;++p) {
        if (*p=='"' || *p=='\\') { s+='\\'; s+=(char)*p; }
        else if (*p<32) { char b[8]; std::snprintf(b,sizeof(b),"\\u%04x",*p); s+=b; }
        else s+=(char)*p;
    }
    s+='"';
}
void rational(Text &s,uint64_t frames,double rate) {
    char b[160]; std::snprintf(b,sizeof(b),"{\"OTIO_SCHEMA\":\"RationalTime.1\",\"value\":%llu,\"rate\":%.17g}",(unsigned long long)frames,rate); s+=b;
}
void range(Text &s,uint64_t start,uint64_t duration,double rate) {
    s+="{\"OTIO_SCHEMA\":\"TimeRange.1\",\"start_time\":"; rational(s,start,rate);
    s+=",\"duration\":"; rational(s,duration,rate); s+='}';
}
const char *str(yyjson_val *v,const char *key) { return yyjson_get_str(yyjson_obj_get(v,key)); }
bool schema(yyjson_val *v,const char *name) { const char *s=str(v,"OTIO_SCHEMA"); return s && !std::strcmp(s,name); }
bool empty_array(yyjson_val *v,const char *key) {
    auto *a=yyjson_obj_get(v,key); return !a || yyjson_is_null(a) || (yyjson_is_arr(a) && !yyjson_arr_size(a));
}
bool plain(yyjson_val *v) {
    auto *enabled=yyjson_obj_get(v,"enabled");
    return (!enabled || (yyjson_is_bool(enabled) && yyjson_get_bool(enabled))) && empty_array(v,"effects") && empty_array(v,"markers");
}
bool frame(yyjson_val *v,double rate,uint64_t &out) {
    auto *value=yyjson_obj_get(v,"value"),*clock=yyjson_obj_get(v,"rate");
    if (!schema(v,"RationalTime.1") || !yyjson_is_num(value) || !yyjson_is_num(clock)) return false;
    double n=yyjson_get_num(value),r=yyjson_get_num(clock);
    if (!std::isfinite(n) || !std::isfinite(r) || r<=0 || n<0) return false;
    double result=opentime::RationalTime(n,r).rescaled_to(rate).value();
    if (!std::isfinite(result) || result>1.e9 || std::fabs(result-std::round(result))>1.e-6) return false;
    out=(uint64_t)std::llround(result); return true;
}
bool source_range(yyjson_val *v,double rate,uint64_t &start,uint64_t &length) {
    auto *r=yyjson_obj_get(v,"source_range");
    return schema(r,"TimeRange.1") && frame(yyjson_obj_get(r,"start_time"),rate,start) && frame(yyjson_obj_get(r,"duration"),rate,length) && length>0;
}
bool no_range(yyjson_val *v) { auto *r=yyjson_obj_get(v,"source_range"); return !r || yyjson_is_null(r); }
bool safe_string(yyjson_val *v,size_t limit) {
    const char *s=yyjson_get_str(v); return s && yyjson_get_len(v)<limit && std::strlen(s)==yyjson_get_len(v);
}
}
extern "C" jfx_result_t jfx_nle_timecode(uint64_t frame,uint32_t num,uint32_t den,bool drop,char *out,size_t cap) {
    if (!num || !den || frame>1000000000 || !out || !cap) return JFX_ERROR_INVALID_ARGUMENT;
    try {
        double rate=(double)num/den;
        auto text=opentime::RationalTime((double)frame,rate).to_timecode(rate,drop?opentime::IsDropFrameRate::ForceYes:opentime::IsDropFrameRate::ForceNo);
        if (text.empty()) return JFX_ERROR_INVALID_ARGUMENT;
        if (text.size()+1>cap) return JFX_ERROR_OUT_OF_MEMORY;
        std::memcpy(out,text.c_str(),text.size()+1); return JFX_SUCCESS;
    } catch (...) { return JFX_ERROR_OUT_OF_MEMORY; }
}
extern "C" jfx_result_t jfx_nle_export_otio(const jfx_editor_t *e,char *out,size_t capacity,size_t *written) {
    if (!e || !out || !written || jfx_editor_kind(e)!=JFX_PROJECT_KIND_SEQUENCE) return JFX_ERROR_INVALID_ARGUMENT;
    try {
        auto *t=jfx_editor_timeline(const_cast<jfx_editor_t *>(e)); double rate=jfx_timeline_fps(t);
        if (jfx_timeline_master_audio_gain(t)!=1) return JFX_ERROR_NOT_IMPLEMENTED;
        Text s="{\"OTIO_SCHEMA\":\"Timeline.1\",\"name\":\"JoltFX sequence\",\"tracks\":{\"OTIO_SCHEMA\":\"Stack.1\",\"children\":[";
        for (uint32_t a=0;a<jfx_timeline_track_count(t);++a) {
            if (jfx_timeline_track_muted(t,a) || jfx_timeline_track_solo(t,a) || jfx_timeline_track_opacity(t,a)!=1 ||
                jfx_timeline_track_blend(t,a)!=JFX_BLEND_NORMAL || jfx_timeline_track_audio_gain(t,a)!=1 ||
                jfx_timeline_audio_insert_count(t,a) || jfx_timeline_audio_automation_count(t,a)) return JFX_ERROR_NOT_IMPLEMENTED;
            bool audio=false; size_t count=jfx_timeline_clip_count(t,a);
            if (count) audio=jfx_timeline_clip_source(t,a,0)==JFX_CLIP_AUDIO;
            if (a) s+=',';
            s+="{\"OTIO_SCHEMA\":\"Track.1\",\"name\":"; quote(s,jfx_timeline_track_name(t,a));
            s+=",\"kind\":"; quote(s,audio?"Audio":"Video"); s+=",\"children\":[";
            uint32_t order[JFX_TIMELINE_MAX_CLIPS_PER_TRACK];
            for (uint32_t b=0;b<count;++b) order[b]=b;
            std::sort(order,order+count,[&](uint32_t x,uint32_t y) {
                auto sx=jfx_timeline_clip_start(t,a,x),sy=jfx_timeline_clip_start(t,a,y); return sx==sy?x<y:sx<sy;
            });
            uint64_t cursor=0; bool comma=false;
            for (size_t k=0;k<count;++k) {
                uint32_t b=order[k]; auto source=jfx_timeline_clip_source(t,a,b);
                uint64_t start=jfx_timeline_clip_start(t,a,b),length=jfx_timeline_clip_length(t,a,b),in=jfx_timeline_clip_in_point(t,a,b);
                jfx_clip_audio_t mix{}; mix.size=sizeof(mix); jfx_timeline_get_clip_audio(t,a,b,&mix);
                if ((source!=JFX_CLIP_AUDIO && source!=JFX_CLIP_VIDEO && source!=JFX_CLIP_IMAGE) ||
                    (source==JFX_CLIP_AUDIO)!=audio || start<cursor || start>1000000000 || length>1000000000 || in>1000000000 ||
                    !jfx_timeline_clip_enabled(t,a,b) || jfx_timeline_clip_opacity(t,a,b)!=1 || jfx_timeline_clip_blend(t,a,b)!=JFX_BLEND_NORMAL ||
                    jfx_timeline_effect_count(t,a,b) || jfx_timeline_clip_transition_frames(t,a,b) || *jfx_timeline_clip_subtitles(t,a,b) ||
                    mix.gain!=1 || mix.pan!=0 || mix.fade_in_frames || mix.fade_out_frames || (source!=JFX_CLIP_IMAGE && !mix.enabled)) return JFX_ERROR_NOT_IMPLEMENTED;
                if (start>cursor) {
                    if (comma) s+=',';
                    s+="{\"OTIO_SCHEMA\":\"Gap.1\",\"source_range\":"; range(s,0,start-cursor,rate); s+='}'; comma=true;
                }
                if (comma) s+=',';
                s+="{\"OTIO_SCHEMA\":\"Clip.2\",\"name\":"; quote(s,jfx_timeline_clip_name(t,a,b));
                s+=",\"source_range\":"; range(s,in,length,rate);
                s+=",\"metadata\":{\"joltfx_source\":"; quote(s,jfx_clip_source_name(source));
                s+="},\"active_media_reference_key\":\"DEFAULT_MEDIA\",\"media_references\":{\"DEFAULT_MEDIA\":{\"OTIO_SCHEMA\":\"ExternalReference.1\",\"target_url\":";
                quote(s,jfx_timeline_clip_path(t,a,b)); s+="}}}"; comma=true; cursor=start+length;
                if (s.size()>JFX_PROJECT_MAX_BYTES) return JFX_ERROR_OUT_OF_MEMORY;
            }
            s+="]}";
        }
        s+="]}}";
        if (s.size()+1>capacity) return JFX_ERROR_OUT_OF_MEMORY;
        std::memcpy(out,s.c_str(),s.size()+1); *written=s.size(); return JFX_SUCCESS;
    } catch (const std::bad_alloc &) { return JFX_ERROR_OUT_OF_MEMORY; }
    catch (...) { return JFX_ERROR_INVALID_ARGUMENT; }
}
extern "C" jfx_result_t jfx_nle_import_otio(jfx_editor_t *e,const char *json,size_t length,uint32_t w,uint32_t h,uint32_t num,uint32_t den) {
    if (!e || !json || !length || length>JFX_PROJECT_MAX_BYTES || !w || !h || w>4096 || h>4096 || !num || !den) return JFX_ERROR_INVALID_ARGUMENT;
    try {
        Json doc{yyjson_read_opts(const_cast<char *>(json),length,0,&allocator,nullptr)};
        if (!doc.p) return JFX_ERROR_INVALID_ARGUMENT;
        auto *root=yyjson_doc_get_root(doc.p),*stack=yyjson_obj_get(root,"tracks");
        if (!schema(root,"Timeline.1") || !schema(stack,"Stack.1") || !plain(root) || !plain(stack) || !no_range(stack)) return JFX_ERROR_NOT_IMPLEMENTED;
        auto *tracks=yyjson_obj_get(stack,"children");
        if (!yyjson_is_arr(tracks) || yyjson_arr_size(tracks)>JFX_TIMELINE_MAX_TRACKS) return JFX_ERROR_INVALID_ARGUMENT;
        Timeline timeline{jfx_timeline_create(w,h,num,den)}; if (!timeline.p) return JFX_ERROR_OUT_OF_MEMORY;
        double rate=(double)num/den;
        auto *origin=yyjson_obj_get(root,"global_start_time"); uint64_t origin_frame=0;
        if (origin && !yyjson_is_null(origin) && (!frame(origin,rate,origin_frame) || origin_frame)) return JFX_ERROR_NOT_IMPLEMENTED;
        for (size_t a=0;a<yyjson_arr_size(tracks);++a) {
            auto *track=yyjson_arr_get(tracks,a); const char *kind=str(track,"kind");
            if (!schema(track,"Track.1") || !plain(track) || !no_range(track) || !kind || (std::strcmp(kind,"Audio") && std::strcmp(kind,"Video"))) return JFX_ERROR_NOT_IMPLEMENTED;
            auto *name=yyjson_obj_get(track,"name");
            if (name && !safe_string(name,JFX_TIMELINE_NAME_MAX)) return JFX_ERROR_INVALID_ARGUMENT;
            uint32_t ti=jfx_timeline_add_track(timeline.p,name?yyjson_get_str(name):"Track");
            if (ti==UINT32_MAX) return JFX_ERROR_OUT_OF_MEMORY;
            auto *children=yyjson_obj_get(track,"children");
            if (!yyjson_is_arr(children) || yyjson_arr_size(children)>256) return JFX_ERROR_INVALID_ARGUMENT;
            uint64_t cursor=0;
            for (size_t b=0;b<yyjson_arr_size(children);++b) {
                auto *item=yyjson_arr_get(children,b); uint64_t in=0,frames=0;
                if (!plain(item) || !source_range(item,rate,in,frames) || frames>1000000000-cursor) return JFX_ERROR_NOT_IMPLEMENTED;
                /* The shared model derives duration from clips, so a trailing
                 * gap cannot be represented without changing editorial timing. */
                if (schema(item,"Gap.1")) {
                    if (b+1==yyjson_arr_size(children)) return JFX_ERROR_NOT_IMPLEMENTED;
                    cursor+=frames; continue;
                }
                if (!schema(item,"Clip.2") && !schema(item,"Clip.1")) return JFX_ERROR_NOT_IMPLEMENTED;
                auto *ref=yyjson_obj_get(item,"media_reference");
                if (schema(item,"Clip.2")) {
                    const char *key=str(item,"active_media_reference_key"); if (!key) return JFX_ERROR_INVALID_ARGUMENT;
                    ref=yyjson_obj_get(yyjson_obj_get(item,"media_references"),key);
                }
                if (!schema(ref,"ExternalReference.1")) return JFX_ERROR_NOT_IMPLEMENTED;
                auto *path=yyjson_obj_get(ref,"target_url"); name=yyjson_obj_get(item,"name");
                if (!safe_string(path,JFX_NODE_PATH_MAX) || (name && !safe_string(name,JFX_TIMELINE_NAME_MAX))) return JFX_ERROR_INVALID_ARGUMENT;
                const char *url=yyjson_get_str(path);
                if (std::strstr(url,"://")) return JFX_ERROR_NOT_IMPLEMENTED;
                jfx_clip_desc_t clip{}; clip.name=name?yyjson_get_str(name):"Clip"; clip.image_path=url;
                clip.source=!std::strcmp(kind,"Audio")?JFX_CLIP_AUDIO:JFX_CLIP_VIDEO;
                const char *source=str(yyjson_obj_get(item,"metadata"),"joltfx_source");
                if (source && !std::strcmp(source,"image") && clip.source==JFX_CLIP_VIDEO) clip.source=JFX_CLIP_IMAGE;
                clip.start_frame=cursor; clip.length_frames=frames; clip.in_point=in; clip.opacity=1; clip.enabled=true;
                if (jfx_timeline_add_clip(timeline.p,ti,&clip)==UINT32_MAX) return JFX_ERROR_INVALID_ARGUMENT;
                cursor+=frames;
            }
        }
        tilly::vector<char> text(JFX_PROJECT_MAX_BYTES); size_t written=0;
        auto r=jfx_project_save_sequence(timeline.p,text.data(),text.size(),&written);
        return r==JFX_SUCCESS?jfx_editor_load(e,text.data(),written,nullptr,0):r;
    } catch (const std::bad_alloc &) { return JFX_ERROR_OUT_OF_MEMORY; }
    catch (...) { return JFX_ERROR_INVALID_ARGUMENT; }
}
