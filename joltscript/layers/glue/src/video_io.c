#include "joltscript/video_io.h"
#include <math.h>
#include <string.h>
#include <stdio.h>
#if defined(JFX_HAVE_FFMPEG)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#endif
const char *jolt_media_ffmpeg_version(void) {
#if defined(JFX_HAVE_FFMPEG)
    return av_version_info();
#else
    return "unavailable";
#endif
}
const char *jolt_media_encoder_at(size_t index,int audio) {
#if defined(JFX_HAVE_FFMPEG)
    void *cursor=NULL; const AVCodec *codec;
    while ((codec=av_codec_iterate(&cursor))) if (av_codec_is_encoder(codec) &&
        codec->type==(audio?AVMEDIA_TYPE_AUDIO:AVMEDIA_TYPE_VIDEO)) {
        if (!index--) return codec->name;
    }
#else
    (void)index; (void)audio;
#endif
    return NULL;
}
int jolt_media_probe(const char *path,jolt_media_info_t *out) {
    if (!path || !*path || strstr(path,"://") || !out || out->size<sizeof(*out)) return -1;
#if defined(JFX_HAVE_FFMPEG)
    AVFormatContext *format=NULL; AVDictionary *options=NULL;
    av_dict_set(&options,"protocol_whitelist","file",0);
    int result=avformat_open_input(&format,path,NULL,&options); av_dict_free(&options);
    if (result<0 || avformat_find_stream_info(format,NULL)<0) { avformat_close_input(&format); return -1; }
    jolt_media_info_t info={.size=sizeof(info)};
    snprintf(info.format,sizeof(info.format),"%s",format->iformat->name);
    if (format->duration!=AV_NOPTS_VALUE) info.duration=(double)format->duration/AV_TIME_BASE;
    int video=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    int audio=av_find_best_stream(format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    if (video>=0) {
        AVStream *stream=format->streams[video]; AVRational fps=av_guess_frame_rate(format,stream,NULL);
        info.video=1; info.width=(uint32_t)stream->codecpar->width; info.height=(uint32_t)stream->codecpar->height;
        if (fps.num>0 && fps.den>0) { info.fps_num=(uint32_t)fps.num; info.fps_den=(uint32_t)fps.den; }
        snprintf(info.video_codec,sizeof(info.video_codec),"%s",avcodec_get_name(stream->codecpar->codec_id));
        if (info.duration<=0 && stream->duration!=AV_NOPTS_VALUE) info.duration=(double)stream->duration*av_q2d(stream->time_base);
    }
    if (audio>=0) { info.audio=1; snprintf(info.audio_codec,sizeof(info.audio_codec),"%s",avcodec_get_name(format->streams[audio]->codecpar->codec_id)); }
    avformat_close_input(&format);
    if (!info.video && !info.audio) return -1;
    *out=info; return 0;
#else
    return -2;
#endif
}
int jolt_video_io_available(void) {
#if defined(JFX_HAVE_FFMPEG)
    return 1;
#else
    return 0;
#endif
}
int jolt_video_io_frame(const char *path, double seconds, uint32_t w, uint32_t h,
    uint8_t *out, size_t capacity) {
    if (!path || !*path || strstr(path, "://") || !out || !w || !h || w>4096 || h>4096 ||
        capacity<(size_t)w*h*4 || !isfinite(seconds) || seconds<0 || seconds>1.e9) return -1;
#if defined(JFX_HAVE_FFMPEG)
    AVFormatContext *format=NULL; AVCodecContext *codec=NULL;
    AVPacket *packet=NULL; AVFrame *frame=NULL;
    struct SwsContext *scale=NULL; int result=-1;
    AVDictionary *options=NULL;
    /* No network protocols or decoder worker threads escape the engine. */
    av_dict_set(&options,"protocol_whitelist","file",0);
    int opened=avformat_open_input(&format,path,NULL,&options);
    av_dict_free(&options);
    if (opened<0 || avformat_find_stream_info(format,NULL)<0) goto done;
    const AVCodec *decoder=NULL;
    int stream=av_find_best_stream(format,AVMEDIA_TYPE_VIDEO,-1,-1,&decoder,0);
    if (stream<0 || !decoder) goto done;
    codec=avcodec_alloc_context3(decoder);
    if (!codec || avcodec_parameters_to_context(codec,format->streams[stream]->codecpar)<0) goto done;
    codec->thread_count=1;
    if (avcodec_open2(codec,decoder,NULL)<0) goto done;
    double timebase=av_q2d(format->streams[stream]->time_base);
    int64_t origin=format->streams[stream]->start_time;
    if (origin==AV_NOPTS_VALUE) origin=0;
    double target_double=seconds/timebase+(double)origin;
    if (!(timebase>0) || !isfinite(target_double) || target_double>=(double)INT64_MAX) goto done;
    int64_t target=(int64_t)target_double;
    if (av_seek_frame(format,stream,target,AVSEEK_FLAG_BACKWARD)<0 && seconds>0) goto done;
    avcodec_flush_buffers(codec);
    packet=av_packet_alloc(); frame=av_frame_alloc();
    if (!packet || !frame) goto done;
    int draining=0;
    /* Bounded decode: malformed or timestamp-free input cannot spin forever. */
    for (unsigned attempts=0;attempts<100000;++attempts) {
        int received=avcodec_receive_frame(codec,frame);
        if (received==0) {
            int64_t pts=frame->best_effort_timestamp;
            if (pts!=AV_NOPTS_VALUE && pts<target) { av_frame_unref(frame); continue; }
            if (frame->width<=0 || frame->height<=0 || frame->width>16384 || frame->height>16384) goto done;
            scale=sws_getContext(frame->width,frame->height,(enum AVPixelFormat)frame->format,
                (int)w,(int)h,AV_PIX_FMT_RGBA,SWS_BILINEAR,NULL,NULL,NULL);
            if (!scale) goto done;
            uint8_t *destination[4]={out,NULL,NULL,NULL}; int stride[4]={(int)(w*4),0,0,0};
            result=sws_scale(scale,(const uint8_t *const *)frame->data,frame->linesize,0,frame->height,destination,stride)==(int)h ? 0 : -1;
            goto done;
        }
        if (received!=AVERROR(EAGAIN) || draining) goto done;
        int read;
        do {
            av_packet_unref(packet); read=av_read_frame(format,packet);
        } while (read>=0 && packet->stream_index!=stream);
        if (read<0) { draining=1; if (avcodec_send_packet(codec,NULL)<0) goto done; }
        else if (avcodec_send_packet(codec,packet)<0) goto done;
    }
done:
    sws_freeContext(scale); av_frame_free(&frame); av_packet_free(&packet);
    avcodec_free_context(&codec); avformat_close_input(&format); return result;
#else
    return -2;
#endif
}
