#include "joltscript/media_analysis.h"
#include "ebur128.h"
#include "tilly/memory.h"
#include <math.h>

jolt_status_t jolt_media_analyze(const float *pcm,size_t frames,uint32_t rate,
    double *lufs,double *peak,uint64_t *onset,double *confidence) {
    if (!pcm || !lufs || !peak || !onset || !confidence || rate<8000 || rate>192000 ||
        frames<rate/10 || frames>(size_t)rate*60) return JOLT_ERR_ARGUMENT;
    for (size_t i=0;i<frames*2;++i) if (!isfinite(pcm[i]) || fabsf(pcm[i])>16) return JOLT_ERR_ARGUMENT;
    ebur128_state *meter=ebur128_init(2,rate,EBUR128_MODE_I|EBUR128_MODE_SAMPLE_PEAK|EBUR128_MODE_HISTOGRAM);
    if (!meter) return JOLT_ERR_MEMORY;
    double loudness=0,left=0,right=0;
    int err=ebur128_add_frames_float(meter,pcm,frames);
    if (!err) err=ebur128_loudness_global(meter,&loudness);
    if (!err) err=ebur128_sample_peak(meter,0,&left);
    if (!err) err=ebur128_sample_peak(meter,1,&right);
    ebur128_destroy(&meter);
    if (err) return JOLT_ERR_MEMORY;
    /* 1 ms energy windows, 50 ms noise history. Reject sustained signals and
     * ambiguous separated impulses rather than silently syncing to speech. */
    size_t window=rate/1000, count=(frames+window-1)/window;
    double *energy=tilly_mem_calloc(count,sizeof(double));
    if (!energy) return JOLT_ERR_MEMORY;
    for (size_t i=0;i<frames;++i) {
        double a=pcm[i*2],b=pcm[i*2+1]; energy[i/window]+=(a*a+b*b)/(2*(double)window);
    }
    double best=0,second=0; size_t at=0;
    for (size_t i=50;i+50<count;++i) {
        double noise=0; for (size_t k=i-50;k<i;++k) noise+=energy[k]/50;
        double score=energy[i]/fmax(1.e-8,noise);
        if (energy[i]<1.e-5 || score<16) continue;
        double tail=0; for (size_t k=i+10;k<i+50;++k) tail+=energy[k]/40;
        if (tail>energy[i]*.25) continue;
        double strength=energy[i];
        if (strength>best) { if (i>at+100) second=fmax(second,best); best=strength; at=i; }
        else if (i>at+100 && strength>second) second=strength;
    }
    uint64_t sample=UINT64_MAX; double certainty=0;
    if (best>0 && second<best*.5) {
        size_t begin=at*window,end=begin+window; if (end>frames) end=frames;
        double maximum=0; sample=begin;
        for (size_t i=begin;i<end;++i) {
            double a=fmax(fabs((double)pcm[i*2]),fabs((double)pcm[i*2+1]));
            if (a>maximum) { maximum=a; sample=i; }
        }
        certainty=1-second/best;
    }
    tilly_mem_free(energy);
    *lufs=loudness; *peak=fmax(left,right); *onset=sample; *confidence=certainty;
    return JOLT_OK;
}
