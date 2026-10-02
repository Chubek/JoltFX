#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#include "joltscript/media_writer.h"
#include "tilly/allocator.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <string.h>
#if defined(JFX_HAVE_FFMPEG)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#endif

bool jolt_media_available(void) {
#if defined(JFX_HAVE_FFMPEG)
    return true;
#else
    return false;
#endif
}
bool jolt_media_codec_available(const char *name,bool audio) {
    if (!name || !*name) return false;
#if defined(JFX_HAVE_FFMPEG)
    const AVCodec *c=avcodec_find_encoder_by_name(name);
    return c && c->type==(audio?AVMEDIA_TYPE_AUDIO:AVMEDIA_TYPE_VIDEO);
#else
    (void)audio; return false;
#endif
}
#if defined(JFX_HAVE_FFMPEG)
struct jolt_media_writer {
    AVFormatContext *format;
    AVIOContext *io;
    AVCodecContext *video,*audio;
    AVStream *video_stream,*audio_stream;
    AVFrame *video_frame,*audio_frame;
    AVPacket *packet;
    AVAudioFifo *fifo;
    struct SwsContext *scale;
    SwrContext *resample;
    int64_t video_pts,audio_pts,audio_total;
    bool finishing,finished;
    uint8_t *flattened;
};
static void *allocate(size_t n) { return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,_Alignof(max_align_t)); }
static void release(void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),p); }
#if LIBAVFORMAT_VERSION_MAJOR >= 61
static int io_write(void *opaque,const uint8_t *data,int n) {
#else
static int io_write(void *opaque,uint8_t *data,int n) {
#endif
    if (n<0) return AVERROR(EINVAL);
    return fwrite(data,1,(size_t)n,(FILE *)opaque)==(size_t)n?n:AVERROR(EIO);
}
static int64_t io_seek(void *opaque,int64_t offset,int whence) {
    FILE *file=opaque; whence &= ~AVSEEK_FORCE;
#if defined(_WIN32)
    int64_t pos=_ftelli64(file);
    if (whence==AVSEEK_SIZE) {
        if (_fseeki64(file,0,SEEK_END)) return AVERROR(EIO);
        int64_t end=_ftelli64(file); if (_fseeki64(file,pos,SEEK_SET)) return AVERROR(EIO); return end;
    }
    return _fseeki64(file,offset,whence)?AVERROR(EIO):_ftelli64(file);
#else
    off_t pos=ftello(file);
    if (whence==AVSEEK_SIZE) {
        if (fseeko(file,0,SEEK_END)) return AVERROR(EIO);
        int64_t end=(int64_t)ftello(file); if (fseeko(file,pos,SEEK_SET)) return AVERROR(EIO); return end;
    }
    return fseeko(file,(off_t)offset,whence)?AVERROR(EIO):(int64_t)ftello(file);
#endif
}
static int packets(jolt_media_writer_t *w,AVCodecContext *codec,AVStream *stream) {
    for (;;) {
        int n=avcodec_receive_packet(codec,w->packet);
        if (n==AVERROR(EAGAIN) || n==AVERROR_EOF) return 0;
        if (n<0) return n;
        if (codec==w->audio && w->finishing && w->packet->pts!=AV_NOPTS_VALUE &&
            w->packet->pts+w->packet->duration>w->audio_total && w->packet->pts>=0) {
            w->packet->duration=w->audio_total-w->packet->pts;
            if (w->packet->duration<=0) { av_packet_unref(w->packet); continue; }
        }
        av_packet_rescale_ts(w->packet,codec->time_base,stream->time_base);
        w->packet->stream_index=stream->index;
        n=av_interleaved_write_frame(w->format,w->packet); av_packet_unref(w->packet);
        if (n<0) return n;
    }
}
static enum AVPixelFormat pixel_format(const AVCodec *codec) {
    const enum AVPixelFormat *formats=NULL;
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const void *configs=NULL;
    if (avcodec_get_supported_config(NULL,codec,AV_CODEC_CONFIG_PIX_FORMAT,0,&configs,NULL)>=0) formats=configs;
#else
    formats=codec->pix_fmts;
#endif
    if (!formats) return AV_PIX_FMT_YUV420P;
    enum AVPixelFormat preferred=AV_PIX_FMT_YUV420P;
    if (codec->id==AV_CODEC_ID_FFV1) preferred=AV_PIX_FMT_BGRA;
    if (codec->id==AV_CODEC_ID_PRORES) preferred=AV_PIX_FMT_YUV422P10LE;
    for (const enum AVPixelFormat *p=formats;*p!=AV_PIX_FMT_NONE;++p) if (*p==preferred) return *p;
    return formats[0];
}
static enum AVSampleFormat sample_format(const AVCodec *codec) {
    const enum AVSampleFormat *formats=NULL;
#if LIBAVCODEC_VERSION_MAJOR >= 61
    const void *configs=NULL;
    if (avcodec_get_supported_config(NULL,codec,AV_CODEC_CONFIG_SAMPLE_FORMAT,0,&configs,NULL)>=0) formats=configs;
#else
    formats=codec->sample_fmts;
#endif
    return formats?formats[0]:AV_SAMPLE_FMT_FLTP;
}
void jolt_media_writer_close(jolt_media_writer_t *w) {
    if (!w) return;
    if (w->io) avio_flush(w->io);
    sws_freeContext(w->scale); swr_free(&w->resample); av_audio_fifo_free(w->fifo);
    av_frame_free(&w->video_frame); av_frame_free(&w->audio_frame); av_packet_free(&w->packet);
    avcodec_free_context(&w->video); avcodec_free_context(&w->audio);
    avformat_free_context(w->format);
    if (w->io) { av_freep(&w->io->buffer); avio_context_free(&w->io); }
    release(w->flattened); release(w);
}
jolt_status_t jolt_media_writer_open(FILE *file,const jolt_media_options_t *o,jolt_media_writer_t **out) {
    if (!file || !o || o->size<sizeof(*o) || !out || !o->container || !o->video_codec || !o->width || !o->height ||
        o->width>4096 || o->height>4096 || !o->fps_num || !o->fps_den || o->fps_num>INT_MAX || o->fps_den>INT_MAX ||
        o->video_bitrate>INT64_MAX || o->audio_bitrate>INT64_MAX ||
        (o->audio && (!o->audio_codec || o->sample_rate<8000 || o->sample_rate>192000))) return JOLT_ERR_ARGUMENT;
    const AVCodec *video=avcodec_find_encoder_by_name(o->video_codec);
    const AVCodec *audio=o->audio?avcodec_find_encoder_by_name(o->audio_codec):NULL;
    if (!video || video->type!=AVMEDIA_TYPE_VIDEO || (o->audio && (!audio || audio->type!=AVMEDIA_TYPE_AUDIO))) return JOLT_ERR_CAPABILITY;
    jolt_media_writer_t *w=allocate(sizeof(*w)); if (!w) return JOLT_ERR_MEMORY;
    memset(w,0,sizeof(*w));
    if (avformat_alloc_output_context2(&w->format,NULL,o->container,NULL)<0 || !w->format) goto fail;
    if ((w->format->oformat->flags & AVFMT_NOFILE) ||
        avformat_query_codec(w->format->oformat,video->id,FF_COMPLIANCE_NORMAL)==0 ||
        (audio && avformat_query_codec(w->format->oformat,audio->id,FF_COMPLIANCE_NORMAL)==0)) goto fail;
    uint8_t *buffer=av_malloc(32768); if (!buffer) goto memory;
    w->io=avio_alloc_context(buffer,32768,1,file,NULL,io_write,io_seek);
    if (!w->io) { av_free(buffer); goto memory; }
    w->format->pb=w->io; w->format->flags|=AVFMT_FLAG_CUSTOM_IO;
    w->packet=av_packet_alloc(); w->video=avcodec_alloc_context3(video);
    w->video_frame=av_frame_alloc(); w->video_stream=avformat_new_stream(w->format,NULL);
    if (!w->packet || !w->video || !w->video_frame || !w->video_stream) goto memory;
    w->video->width=(int)o->width; w->video->height=(int)o->height;
    w->video->time_base=(AVRational){(int)o->fps_den,(int)o->fps_num};
    w->video->framerate=(AVRational){(int)o->fps_num,(int)o->fps_den};
    w->video->pix_fmt=pixel_format(video); w->video->thread_count=1;
    const AVPixFmtDescriptor *pixel_desc=av_pix_fmt_desc_get(w->video->pix_fmt);
    if (!pixel_desc) goto fail;
    if (!(pixel_desc->flags & AV_PIX_FMT_FLAG_ALPHA)) {
        w->flattened=allocate((size_t)o->width*o->height*4);
        if (!w->flattened) goto memory;
    }
    w->video->bit_rate=(int64_t)(o->video_bitrate?o->video_bitrate:8000000);
    w->video->gop_size=30; w->video->max_b_frames=0;
    if (w->format->oformat->flags & AVFMT_GLOBALHEADER) w->video->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
    if (avcodec_open2(w->video,video,NULL)<0) goto fail;
    if (avcodec_parameters_from_context(w->video_stream->codecpar,w->video)<0) goto memory;
    w->video_stream->time_base=w->video->time_base; w->video_stream->avg_frame_rate=w->video->framerate;
    w->video_frame->format=w->video->pix_fmt; w->video_frame->width=w->video->width; w->video_frame->height=w->video->height;
    if (av_frame_get_buffer(w->video_frame,32)<0) goto memory;
    w->scale=sws_getContext((int)o->width,(int)o->height,AV_PIX_FMT_RGBA,(int)o->width,(int)o->height,w->video->pix_fmt,SWS_BICUBIC,NULL,NULL,NULL);
    if (!w->scale) goto memory;
    if (audio) {
        w->audio=avcodec_alloc_context3(audio); w->audio_frame=av_frame_alloc();
        w->audio_stream=avformat_new_stream(w->format,NULL);
        if (!w->audio || !w->audio_frame || !w->audio_stream) goto memory;
        w->audio->sample_rate=(int)o->sample_rate; w->audio->time_base=(AVRational){1,(int)o->sample_rate};
        av_channel_layout_default(&w->audio->ch_layout,2); w->audio->sample_fmt=sample_format(audio);
        w->audio->bit_rate=(int64_t)(o->audio_bitrate?o->audio_bitrate:192000); w->audio->thread_count=1;
        if (w->format->oformat->flags & AVFMT_GLOBALHEADER) w->audio->flags|=AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(w->audio,audio,NULL)<0) goto fail;
        if (avcodec_parameters_from_context(w->audio_stream->codecpar,w->audio)<0) goto memory;
        w->audio_stream->time_base=w->audio->time_base;
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&w->resample,&w->audio->ch_layout,w->audio->sample_fmt,(int)o->sample_rate,
            &stereo,AV_SAMPLE_FMT_FLT,(int)o->sample_rate,0,NULL)<0 || swr_init(w->resample)<0) goto fail;
        w->fifo=av_audio_fifo_alloc(w->audio->sample_fmt,2,1);
        if (!w->fifo) goto memory;
    }
    if (avformat_write_header(w->format,NULL)<0 || w->io->error<0) goto fail;
    *out=w; return JOLT_OK;
memory:
    jolt_media_writer_close(w); return JOLT_ERR_MEMORY;
fail:
    jolt_media_writer_close(w); return JOLT_ERR_SYNTAX;
}
jolt_status_t jolt_media_writer_video(jolt_media_writer_t *w,const uint8_t *rgba) {
    if (!w || !rgba || w->finished) return JOLT_ERR_ARGUMENT;
    if (av_frame_make_writable(w->video_frame)<0) return JOLT_ERR_MEMORY;
    if (w->flattened) {
        size_t count=(size_t)w->video->width*(size_t)w->video->height;
        for (size_t i=0;i<count;++i) {
            for (size_t c=0;c<3;++c) w->flattened[i*4+c]=(uint8_t)(((unsigned)rgba[i*4+c]*rgba[i*4+3]+127)/255);
            w->flattened[i*4+3]=255;
        }
        rgba=w->flattened;
    }
    const uint8_t *data[4]={rgba,NULL,NULL,NULL}; int stride[4]={w->video->width*4,0,0,0};
    if (sws_scale(w->scale,data,stride,0,w->video->height,w->video_frame->data,w->video_frame->linesize)!=w->video->height) return JOLT_ERR_SYNTAX;
    w->video_frame->pts=w->video_pts++;
    return avcodec_send_frame(w->video,w->video_frame)<0 || packets(w,w->video,w->video_stream)<0 || w->io->error<0?JOLT_ERR_SYNTAX:JOLT_OK;
}
static int encode_audio(jolt_media_writer_t *w,int count,bool last) {
    av_frame_unref(w->audio_frame);
    int full=w->audio->frame_size?w->audio->frame_size:count;
    int samples=last && (w->audio->codec->capabilities & AV_CODEC_CAP_SMALL_LAST_FRAME)?count:full;
    w->audio_frame->nb_samples=samples; w->audio_frame->format=w->audio->sample_fmt;
    w->audio_frame->sample_rate=w->audio->sample_rate;
    if (av_channel_layout_copy(&w->audio_frame->ch_layout,&w->audio->ch_layout)<0 || av_frame_get_buffer(w->audio_frame,0)<0) return -1;
    av_samples_set_silence(w->audio_frame->data,0,samples,2,w->audio->sample_fmt);
    if (av_audio_fifo_read(w->fifo,(void **)w->audio_frame->data,count)!=count) return -1;
    w->audio_frame->pts=w->audio_pts; w->audio_pts+=samples;
    if (avcodec_send_frame(w->audio,w->audio_frame)<0) return -1;
    return packets(w,w->audio,w->audio_stream);
}
jolt_status_t jolt_media_writer_audio(jolt_media_writer_t *w,const float *stereo,size_t frames) {
    if (!w || !w->audio || !stereo || !frames || frames>65536 || w->finished || w->audio_total>INT64_MAX-(int64_t)frames) return JOLT_ERR_ARGUMENT;
    float *input=allocate(frames*2*sizeof(float)); if (!input) return JOLT_ERR_MEMORY;
    for (size_t i=0;i<frames*2;++i) {
        if (!isfinite(stereo[i])) { release(input); return JOLT_ERR_NUMERIC; }
        input[i]=fmaxf(-1,fminf(1,stereo[i]));
    }
    uint8_t **data=NULL; int linesize;
    if (av_samples_alloc_array_and_samples(&data,&linesize,2,(int)frames,w->audio->sample_fmt,0)<0) { release(input); return JOLT_ERR_MEMORY; }
    const uint8_t *src=(const uint8_t *)input;
    int n=swr_convert(w->resample,data,(int)frames,&src,(int)frames);
    release(input);
    int ok=n>=0 && av_audio_fifo_write(w->fifo,(void **)data,n)==n;
    av_freep(&data[0]); av_freep(&data);
    if (!ok) return JOLT_ERR_SYNTAX;
    w->audio_total+=(int64_t)frames;
    int full=w->audio->frame_size?w->audio->frame_size:1024;
    while (av_audio_fifo_size(w->fifo)>=full) if (encode_audio(w,full,false)<0) return JOLT_ERR_SYNTAX;
    return w->io->error<0?JOLT_ERR_SYNTAX:JOLT_OK;
}
jolt_status_t jolt_media_writer_finish(jolt_media_writer_t *w) {
    if (!w || w->finished) return JOLT_ERR_ARGUMENT;
    w->finishing=true;
    if (avcodec_send_frame(w->video,NULL)<0 || packets(w,w->video,w->video_stream)<0) return JOLT_ERR_SYNTAX;
    if (w->audio) {
        int remaining=av_audio_fifo_size(w->fifo);
        if (remaining && encode_audio(w,remaining,true)<0) return JOLT_ERR_SYNTAX;
        if (avcodec_send_frame(w->audio,NULL)<0 || packets(w,w->audio,w->audio_stream)<0) return JOLT_ERR_SYNTAX;
        w->audio_stream->duration=w->audio_total;
    }
    int result=av_write_trailer(w->format); avio_flush(w->io); w->finished=true;
    return result<0 || w->io->error<0?JOLT_ERR_SYNTAX:JOLT_OK;
}
#else
struct jolt_media_writer { int unused; };
jolt_status_t jolt_media_writer_open(FILE *f,const jolt_media_options_t *o,jolt_media_writer_t **out) {
    if (!f || !o || o->size<sizeof(*o) || !out) return JOLT_ERR_ARGUMENT;
    return JOLT_ERR_CAPABILITY;
}
jolt_status_t jolt_media_writer_video(jolt_media_writer_t *w,const uint8_t *rgba) { return !w || !rgba?JOLT_ERR_ARGUMENT:JOLT_ERR_CAPABILITY; }
jolt_status_t jolt_media_writer_audio(jolt_media_writer_t *w,const float *pcm,size_t n) { return !w || !pcm || !n || n>65536?JOLT_ERR_ARGUMENT:JOLT_ERR_CAPABILITY; }
jolt_status_t jolt_media_writer_finish(jolt_media_writer_t *w) { return !w?JOLT_ERR_ARGUMENT:JOLT_ERR_CAPABILITY; }
void jolt_media_writer_close(jolt_media_writer_t *w) { (void)w; }
#endif
