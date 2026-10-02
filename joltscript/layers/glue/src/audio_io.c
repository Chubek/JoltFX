#include "joltscript/audio_io.h"
#include "tilly/allocator.h"
#include <miniaudio.h>
#include <stdbool.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <limits.h>
#if defined(JFX_HAVE_FFMPEG)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#endif

static void *allocate(size_t n) { return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,_Alignof(max_align_t)); }
static void release(void *p) { tilly_free((tilly_allocator_t *)tilly_default_allocator(),p); }
static void *decoder_alloc(size_t n,void *user) { (void)user; return allocate(n); }
static void *decoder_realloc(void *p,size_t n,void *user) { (void)user; return tilly_realloc((tilly_allocator_t *)tilly_default_allocator(),p,n); }
static void decoder_free(void *p,void *user) { (void)user; release(p); }
struct jolt_audio_reader {
    ma_decoder decoder;
    bool native;
    uint32_t rate, source_rate;
    uint64_t cursor;
#if defined(JFX_HAVE_FFMPEG)
    AVFormatContext *format;
    AVCodecContext *codec;
    AVPacket *packet;
    AVFrame *frame;
    SwrContext *resample;
    int stream;
    bool draining, eof;
    float *pending;
    size_t pending_frames, pending_offset;
    uint64_t silence;
#endif
};
void jolt_audio_reader_close(jolt_audio_reader_t *r) {
    if (!r) return;
    if (r->native) ma_decoder_uninit(&r->decoder);
#if defined(JFX_HAVE_FFMPEG)
    swr_free(&r->resample); av_frame_free(&r->frame); av_packet_free(&r->packet);
    avcodec_free_context(&r->codec); avformat_close_input(&r->format); release(r->pending);
#endif
    release(r);
}
jolt_status_t jolt_audio_reader_open(const char *path,uint32_t rate,jolt_audio_reader_t **out) {
    if (!path || !*path || strstr(path,"://") || !out || rate<8000 || rate>192000) return JOLT_ERR_ARGUMENT;
    FILE *check=fopen(path,"rb"); if (!check) return JOLT_ERR_NOT_FOUND; fclose(check);
    jolt_audio_reader_t *r=allocate(sizeof(*r)); if (!r) return JOLT_ERR_MEMORY;
    memset(r,0,sizeof(*r)); r->rate=rate;
    /* Decode at the native rate; deterministic linear sampling makes arbitrary
     * seeks and different block boundaries identical, including resampling. */
    ma_decoder_config config=ma_decoder_config_init(ma_format_f32,2,0);
    config.allocationCallbacks=(ma_allocation_callbacks){NULL,decoder_alloc,decoder_realloc,decoder_free};
    if (ma_decoder_init_file(path,&config,&r->decoder)==MA_SUCCESS) {
        r->native=true; r->source_rate=r->decoder.outputSampleRate;
        if (!r->source_rate || r->source_rate>384000) { jolt_audio_reader_close(r); return JOLT_ERR_ARGUMENT; }
        *out=r; return JOLT_OK;
    }
#if defined(JFX_HAVE_FFMPEG)
    AVDictionary *options=NULL; av_dict_set(&options,"protocol_whitelist","file",0);
    int opened=avformat_open_input(&r->format,path,NULL,&options); av_dict_free(&options);
    if (opened<0 || avformat_find_stream_info(r->format,NULL)<0) goto fail;
    const AVCodec *decoder=NULL;
    r->stream=av_find_best_stream(r->format,AVMEDIA_TYPE_AUDIO,-1,-1,&decoder,0);
    if (r->stream==AVERROR_STREAM_NOT_FOUND) { jolt_audio_reader_close(r); return JOLT_AUDIO_NO_STREAM; }
    if (r->stream<0 || !decoder) goto fail;
    r->codec=avcodec_alloc_context3(decoder);
    if (!r->codec || avcodec_parameters_to_context(r->codec,r->format->streams[r->stream]->codecpar)<0) goto fail;
    r->codec->thread_count=1;
    if (avcodec_open2(r->codec,decoder,NULL)<0 || r->codec->sample_rate<=0 || r->codec->sample_rate>384000) goto fail;
    AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&r->resample,&stereo,AV_SAMPLE_FMT_FLT,(int)rate,
        &r->codec->ch_layout,r->codec->sample_fmt,r->codec->sample_rate,0,NULL)<0 || swr_init(r->resample)<0) goto fail;
    r->packet=av_packet_alloc(); r->frame=av_frame_alloc();
    r->pending=allocate(65536u*2*sizeof(float));
    if (!r->packet || !r->frame || !r->pending) { jolt_audio_reader_close(r); return JOLT_ERR_MEMORY; }
    /* A delayed audio stream in a video container retains its offset. */
    AVStream *st=r->format->streams[r->stream];
    if (st->start_time!=AV_NOPTS_VALUE && r->format->start_time!=AV_NOPTS_VALUE) {
        int64_t origin=av_rescale_q(r->format->start_time,AV_TIME_BASE_Q,st->time_base);
        if (st->start_time>origin) r->silence=(uint64_t)av_rescale_q(st->start_time-origin,st->time_base,(AVRational){1,(int)rate});
    }
    *out=r; return JOLT_OK;
fail:
    jolt_audio_reader_close(r); return JOLT_ERR_SYNTAX;
#else
    jolt_audio_reader_close(r); return JOLT_ERR_CAPABILITY;
#endif
}

static jolt_status_t native_read(jolt_audio_reader_t *r,uint64_t sample,size_t count,float *out) {
    uint64_t q=sample/r->rate,rem=sample%r->rate;
    if (q>UINT64_MAX/r->source_rate) return JOLT_ERR_ARGUMENT;
    uint64_t first=q*r->source_rate,tail=rem*r->source_rate/r->rate;
    if (tail>UINT64_MAX-first) return JOLT_ERR_ARGUMENT;
    first+=tail;
    uint64_t phase=rem*r->source_rate%r->rate;
    size_t needed=(size_t)((phase+(uint64_t)(count-1)*r->source_rate)/r->rate)+2;
    if (needed>3145730u) return JOLT_ERR_BUDGET;
    if (first>UINT64_MAX-needed) return JOLT_ERR_ARGUMENT;
    float *input=allocate(needed*2*sizeof(float)); if (!input) return JOLT_ERR_MEMORY;
    memset(input,0,needed*2*sizeof(float));
    if (first!=r->cursor && ma_decoder_seek_to_pcm_frame(&r->decoder,first)!=MA_SUCCESS) {
        ma_uint64 length=0;
        if (ma_decoder_get_length_in_pcm_frames(&r->decoder,&length)==MA_SUCCESS && first>=length) {
            memset(out,0,count*2*sizeof(float)); release(input); return JOLT_OK;
        }
        release(input); return JOLT_ERR_SYNTAX;
    }
    ma_uint64 read=0; ma_result status=ma_decoder_read_pcm_frames(&r->decoder,input,(ma_uint64)needed,&read);
    r->cursor=first+read;
    if (status!=MA_SUCCESS && status!=MA_AT_END) { release(input); return JOLT_ERR_SYNTAX; }
    for (size_t i=0;i<count;++i) {
        uint64_t pos=phase+(uint64_t)i*r->source_rate; size_t index=(size_t)(pos/r->rate);
        float f=(float)(pos%r->rate)/(float)r->rate;
        for (size_t c=0;c<2;++c) out[i*2+c]=input[index*2+c]+(input[(index+1)*2+c]-input[index*2+c])*f;
    }
    release(input); return JOLT_OK;
}
#if defined(JFX_HAVE_FFMPEG)
static jolt_status_t next_audio(jolt_audio_reader_t *r) {
    r->pending_frames=r->pending_offset=0;
    for (;;) {
        int received=avcodec_receive_frame(r->codec,r->frame);
        if (received==0) {
            int required=swr_get_out_samples(r->resample,r->frame->nb_samples);
            if (required<0 || required>65536) return JOLT_ERR_BUDGET;
            uint8_t *dest=(uint8_t *)r->pending;
            int n=swr_convert(r->resample,&dest,65536,(const uint8_t **)r->frame->extended_data,r->frame->nb_samples);
            av_frame_unref(r->frame);
            if (n<0) return JOLT_ERR_SYNTAX;
            r->pending_frames=(size_t)n; if (n) return JOLT_OK;
            continue;
        }
        if (received==AVERROR_EOF) {
            uint8_t *dest=(uint8_t *)r->pending;
            int n=swr_convert(r->resample,&dest,65536,NULL,0);
            if (n<0) return JOLT_ERR_SYNTAX;
            r->pending_frames=(size_t)n; r->eof=n==0; return JOLT_OK;
        }
        if (received!=AVERROR(EAGAIN) || r->draining) return JOLT_ERR_SYNTAX;
        int read;
        do {
            read=av_read_frame(r->format,r->packet);
            if (read<0) break;
            if (r->packet->stream_index==r->stream) break;
            av_packet_unref(r->packet);
        } while (true);
        if (read<0) {
            if (read!=AVERROR_EOF || avcodec_send_packet(r->codec,NULL)<0) return JOLT_ERR_SYNTAX;
            r->draining=true;
        } else {
            int sent=avcodec_send_packet(r->codec,r->packet); av_packet_unref(r->packet);
            if (sent<0) return JOLT_ERR_SYNTAX;
        }
    }
}
static jolt_status_t ffmpeg_read(jolt_audio_reader_t *r,uint64_t sample,size_t frames,float *out) {
    if (sample<r->cursor) {
        AVStream *st=r->format->streams[r->stream];
        int64_t start=st->start_time==AV_NOPTS_VALUE?0:st->start_time;
        if (av_seek_frame(r->format,r->stream,start,AVSEEK_FLAG_BACKWARD)<0) return JOLT_ERR_SYNTAX;
        avcodec_flush_buffers(r->codec); swr_close(r->resample);
        if (swr_init(r->resample)<0) return JOLT_ERR_SYNTAX;
        r->draining=r->eof=false; r->pending_frames=r->pending_offset=0; r->cursor=0;
        r->silence=0;
        if (st->start_time!=AV_NOPTS_VALUE && r->format->start_time!=AV_NOPTS_VALUE) {
            int64_t origin=av_rescale_q(r->format->start_time,AV_TIME_BASE_Q,st->time_base);
            if (st->start_time>origin) r->silence=(uint64_t)av_rescale_q(st->start_time-origin,st->time_base,(AVRational){1,(int)r->rate});
        }
    }
    size_t written=0; memset(out,0,frames*2*sizeof(float));
    while (written<frames) {
        uint64_t wanted=sample+(uint64_t)written;
        if (r->silence) {
            uint64_t skip=wanted>r->cursor?wanted-r->cursor:0;
            if (skip>r->silence) skip=r->silence;
            r->silence-=skip; r->cursor+=skip;
            if (r->silence) {
                size_t n=frames-written; if ((uint64_t)n>r->silence) n=(size_t)r->silence;
                r->silence-=n; r->cursor+=n; written+=n; continue;
            }
        }
        if (r->pending_offset==r->pending_frames) {
            if (r->eof) return JOLT_OK;
            jolt_status_t status=next_audio(r); if (status!=JOLT_OK) return status;
            if (!r->pending_frames) continue;
        }
        size_t available=r->pending_frames-r->pending_offset;
        if (wanted>r->cursor) {
            uint64_t skip=wanted-r->cursor; if (skip>available) skip=available;
            r->pending_offset+=(size_t)skip; r->cursor+=skip; continue;
        }
        size_t n=frames-written; if (n>available) n=available;
        memcpy(out+written*2,r->pending+r->pending_offset*2,n*2*sizeof(float));
        r->pending_offset+=n; r->cursor+=n; written+=n;
    }
    return JOLT_OK;
}
#endif
jolt_status_t jolt_audio_reader_read(jolt_audio_reader_t *r,uint64_t sample,size_t frames,float *out) {
    if (!r || !out || !frames || frames>65536 || sample>INT64_MAX-frames) return JOLT_ERR_ARGUMENT;
    jolt_status_t status;
    if (r->native) status=native_read(r,sample,frames,out);
#if defined(JFX_HAVE_FFMPEG)
    else status=ffmpeg_read(r,sample,frames,out);
#else
    else status=JOLT_ERR_CAPABILITY;
#endif
    if (status==JOLT_OK) for (size_t i=0;i<frames*2;++i) if (!isfinite(out[i])) return JOLT_ERR_NUMERIC;
    return status;
}
