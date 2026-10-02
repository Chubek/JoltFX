#import "JFXMobilePlayerBridge.h"
#import <AVFoundation/AVFoundation.h>
#include <math.h>

#include "jfx/mobile_player.h"
#include "jfx/jfx_color.h"
#include "tilly/containers.h"

@interface JFXMobilePlayerBridge () {
    jfx_mobile_player_t *_player;
    jfx_audio_mixer_t *_audioMixer;
    jfx_export_job_t *_exportJob;
    AVAudioEngine *_audioEngine;
    AVAudioPlayerNode *_audioNode;
    AVAudioFormat *_audioFormat;
    uint64_t _audioSample, _audioWritten;
}
@end

@implementation JFXMobilePlayerBridge

- (nullable instancetype)initWithWidth:(NSUInteger)width height:(NSUInteger)height
                              duration:(NSTimeInterval)duration {
    self = [super init];
    if (!self) return nil;
    jfx_mobile_player_config_t config = {
        .size = sizeof(config), .width = (uint32_t)width, .height = (uint32_t)height,
        .duration_seconds = duration, .backend_name = "metal",
    };
    if (jfx_mobile_player_create(&config, &_player) != JFX_SUCCESS) return nil;
    return self;
}

- (void)dealloc { [self stopAudio]; [self cancelVideoExport]; jfx_mobile_player_destroy(_player); }
- (BOOL)tap { return jfx_mobile_player_tap(_player) == JFX_SUCCESS; }
- (BOOL)swipeByPixels:(CGFloat)pixels { return jfx_mobile_player_swipe(_player, pixels) == JFX_SUCCESS; }
- (BOOL)pinchByFactor:(CGFloat)factor { return jfx_mobile_player_pinch(_player, factor) == JFX_SUCCESS; }
- (BOOL)renderElapsed:(NSTimeInterval)elapsed { return jfx_mobile_player_render(_player, elapsed) == JFX_SUCCESS; }

- (NSArray<NSDictionary *> *)colorOperators {
    char *json=tilly_container_alloc(65536);
    NSArray *result=@[];
    if (json && jfx_color_catalog(json,65536)==JFX_SUCCESS) {
        NSData *data=[[NSString stringWithUTF8String:json] dataUsingEncoding:NSUTF8StringEncoding];
        result=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil] ?: @[];
    }
    tilly_container_free(json); return result;
}
- (NSArray<NSDictionary *> *)colorLayers:(NSString *)section track:(NSUInteger)track clip:(NSUInteger)clip {
    jfx_color_section_t wanted=[section isEqualToString:@"calibration"]?JFX_COLOR_CALIBRATION:JFX_COLOR_GRADING;
    if (track>UINT32_MAX || clip>UINT32_MAX) return @[];
    jfx_timeline_t *t=jfx_editor_timeline(jfx_mobile_player_editor(_player));
    NSMutableArray *layers=[NSMutableArray array];
    for (uint32_t i=0;i<jfx_timeline_effect_count(t,(uint32_t)track,(uint32_t)clip);++i) {
        const jfx_node_kind_t *k=jfx_timeline_effect_kind_desc(t,(uint32_t)track,(uint32_t)clip,i);
        if (jfx_color_section(k)!=wanted) continue;
        NSMutableArray *values=[NSMutableArray array];
        for (size_t p=0;p<k->param_count;++p) [values addObject:@(jfx_timeline_effect_param(t,(uint32_t)track,(uint32_t)clip,i,p))];
        const char *path=k->string_count?jfx_timeline_effect_string(t,(uint32_t)track,(uint32_t)clip,i,0):NULL;
        [layers addObject:@{@"name":@(k->name), @"values":values, @"path":path?@(path):@"",
            @"enabled":@(jfx_timeline_effect_enabled(t,(uint32_t)track,(uint32_t)clip,i))}];
    }
    return layers;
}
- (BOOL)editColor:(NSString *)operation track:(NSUInteger)track clip:(NSUInteger)clip
           layer:(NSUInteger)layer value:(double)value text:(NSString *)text {
    if (track>UINT32_MAX || clip>UINT32_MAX || layer>UINT32_MAX ||
        (![operation hasPrefix:@"grade."] && ![operation hasPrefix:@"calibration."])) return NO;
    return jfx_mobile_player_edit(_player,operation.UTF8String,(uint32_t)track,(uint32_t)clip,
        (uint32_t)layer,value,text.UTF8String)==JFX_SUCCESS;
}
- (nullable NSData *)previewRGBA {
    NSMutableData *pixels=[NSMutableData dataWithLength:320*180*4];
    jfx_mobile_player_state_t state={.size=sizeof(state)};
    if (jfx_mobile_player_get_state(_player,&state)!=JFX_SUCCESS ||
        jfx_editor_render(jfx_mobile_player_editor(_player),state.time_seconds,320,180,pixels.mutableBytes,pixels.length)!=JFX_SUCCESS) return nil;
    return pixels;
}
- (NSDictionary *)sequenceState {
    size_t capacity=4*1024*1024; char *json=tilly_container_alloc(capacity); NSDictionary *result=@{};
    if (json && jfx_mobile_player_sequence_state(_player,json,capacity)==JFX_SUCCESS) {
        NSData *data=[[NSString stringWithUTF8String:json] dataUsingEncoding:NSUTF8StringEncoding];
        result=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil] ?: @{};
    }
    tilly_container_free(json); return result;
}
- (BOOL)edit:(NSString *)operation track:(NSUInteger)track clip:(NSUInteger)clip
      target:(NSUInteger)target value:(double)value text:(NSString *)text {
    if (track>UINT32_MAX || clip>UINT32_MAX || target>UINT32_MAX) return NO;
    BOOL success=jfx_mobile_player_edit(_player,operation.UTF8String,(uint32_t)track,(uint32_t)clip,
        (uint32_t)target,value,text.UTF8String)==JFX_SUCCESS;
    if (success) [self stopAudio];
    return success;
}
- (BOOL)seekSeconds:(NSTimeInterval)seconds { return jfx_mobile_player_seek(_player,seconds)==JFX_SUCCESS; }
- (BOOL)loadDocument:(NSString *)document {
    [self stopAudio];
    NSData *data=[document dataUsingEncoding:NSUTF8StringEncoding];
    return jfx_mobile_player_load_document(_player,data.bytes,data.length,NULL,0)==JFX_SUCCESS;
}
- (nullable NSString *)saveDocument {
    char *text=tilly_container_alloc(JFX_PROJECT_MAX_BYTES); size_t n=0; NSString *result=nil;
    if (text && jfx_mobile_player_save_document(_player,text,JFX_PROJECT_MAX_BYTES,&n)==JFX_SUCCESS)
        result=[[NSString alloc] initWithBytes:text length:n encoding:NSUTF8StringEncoding];
    tilly_container_free(text); return result;
}
- (BOOL)writeFrame:(NSUInteger)frame path:(NSString *)path {
    return jfx_mobile_player_write_frame(_player,(uint64_t)frame,path.UTF8String)==JFX_SUCCESS;
}
- (void)stopAudio {
    [_audioNode stop]; [_audioEngine stop];
    _audioNode=nil; _audioEngine=nil; _audioFormat=nil;
    jfx_audio_mixer_destroy(_audioMixer); _audioMixer=NULL; _audioWritten=0;
}
- (BOOL)pumpAudioAtSeconds:(NSTimeInterval)seconds {
    if (!isfinite(seconds) || seconds<0 || seconds>1.e9) return NO;
    if (jfx_editor_kind(jfx_mobile_player_editor(_player))!=JFX_PROJECT_KIND_SEQUENCE) { [self stopAudio]; return YES; }
    uint64_t now=(uint64_t)(seconds*48000);
    if (_audioMixer && fabs((double)_audioSample/48000-seconds)>0.4) [self stopAudio];
    if (!_audioMixer) {
        if (jfx_mobile_player_audio_mixer(_player,48000,&_audioMixer)!=JFX_SUCCESS) return NO;
        AVAudioSession *session=AVAudioSession.sharedInstance;
        if (![session setCategory:AVAudioSessionCategoryPlayback error:nil] || ![session setActive:YES error:nil]) { [self stopAudio]; return NO; }
        _audioEngine=[[AVAudioEngine alloc] init]; _audioNode=[[AVAudioPlayerNode alloc] init];
        _audioFormat=[[AVAudioFormat alloc] initStandardFormatWithSampleRate:48000 channels:2];
        [_audioEngine attachNode:_audioNode]; [_audioEngine connect:_audioNode to:_audioEngine.mainMixerNode format:_audioFormat];
        if (![_audioEngine startAndReturnError:nil]) { [self stopAudio]; return NO; }
        [_audioNode play]; _audioSample=now;
    }
    AVAudioTime *renderTime=_audioNode.lastRenderTime;
    AVAudioTime *played=renderTime?[_audioNode playerTimeForNodeTime:renderTime]:nil;
    uint64_t consumed=played && played.isSampleTimeValid && played.sampleTime>0?(uint64_t)played.sampleTime:0;
    if (_audioWritten>consumed+8192) return YES;
    AVAudioPCMBuffer *buffer=[[AVAudioPCMBuffer alloc] initWithPCMFormat:_audioFormat frameCapacity:4096];
    float *pcm=tilly_container_alloc(8192*sizeof(float));
    if (!buffer || !pcm) { tilly_container_free(pcm); [self stopAudio]; return NO; }
    jfx_result_t result=jfx_audio_mixer_render(_audioMixer,_audioSample,4096,pcm,8192);
    if (result==JFX_SUCCESS) {
        buffer.frameLength=4096;
        for (NSUInteger i=0;i<4096;++i) { buffer.floatChannelData[0][i]=pcm[i*2]; buffer.floatChannelData[1][i]=pcm[i*2+1]; }
        [_audioNode scheduleBuffer:buffer completionHandler:nil]; _audioSample+=4096; _audioWritten+=4096;
    }
    tilly_container_free(pcm);
    if (result!=JFX_SUCCESS) [self stopAudio];
    return result==JFX_SUCCESS;
}
- (BOOL)beginVideoExport:(NSString *)path startFrame:(NSUInteger)start frameCount:(NSUInteger)count audio:(BOOL)audio {
    if (_exportJob) return NO;
    jfx_export_options_t options={.size=sizeof(options),.path=path.UTF8String,
        .start_frame=start,.frame_count=count,.audio=audio};
    return jfx_mobile_player_export_begin(_player,&options,&_exportJob)==JFX_SUCCESS;
}
- (NSDictionary *)stepVideoExport {
    if (!_exportJob) return @{@"result":@(JFX_ERROR_INVALID_ARGUMENT),@"state":@(JFX_EXPORT_FAILED),@"frames":@0};
    jfx_result_t result=jfx_export_step(_exportJob,1);
    NSDictionary *state=@{@"result":@(result),@"state":@(jfx_export_state(_exportJob)),@"frames":@(jfx_export_completed_frames(_exportJob))};
    if (jfx_export_state(_exportJob)!=JFX_EXPORT_RUNNING) [self cancelVideoExport];
    return state;
}
- (void)cancelVideoExport { jfx_export_destroy(_exportJob); _exportJob=NULL; }
- (NSArray<NSDictionary *> *)nodeKinds {
    size_t cap=1024*1024; char *json=tilly_container_alloc(cap); NSArray *result=@[];
    if (json && jfx_node_catalog(json,cap)==JFX_SUCCESS) {
        NSData *data=[[NSString stringWithUTF8String:json] dataUsingEncoding:NSUTF8StringEncoding];
        result=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil] ?: @[];
    }
    tilly_container_free(json); return result;
}
- (NSDictionary *)graphState {
    size_t cap=1024*1024; char *json=tilly_container_alloc(cap); NSDictionary *result=@{};
    if (json && jfx_mobile_player_graph_state(_player,json,cap)==JFX_SUCCESS) {
        NSData *data=[[NSString stringWithUTF8String:json] dataUsingEncoding:NSUTF8StringEncoding];
        result=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil] ?: @{};
    }
    tilly_container_free(json); return result;
}
- (BOOL)loadComposition:(NSString *)document {
    [self stopAudio];
    NSData *data=[document dataUsingEncoding:NSUTF8StringEncoding]; jfx_project_kind_t kind;
    if (!data.length || data.length>JFX_PROJECT_MAX_BYTES ||
        jfx_project_kind_of(data.bytes,data.length,&kind,NULL,0)!=JFX_SUCCESS || kind!=JFX_PROJECT_KIND_GRAPH) return NO;
    return jfx_mobile_player_load_document(_player,data.bytes,data.length,NULL,0)==JFX_SUCCESS;
}
- (nullable NSData *)previewGraphNode:(NSUInteger)node seconds:(NSTimeInterval)seconds {
    if (node!=NSNotFound && node>UINT32_MAX) return nil;
    NSMutableData *pixels=[NSMutableData dataWithLength:320*180*4];
    if (jfx_mobile_player_render_graph(_player,node==NSNotFound?UINT32_MAX:(uint32_t)node,seconds,320,180,pixels.mutableBytes,pixels.length)!=JFX_SUCCESS) return nil;
    return pixels;
}
- (BOOL)writeCompositionAtSeconds:(NSTimeInterval)seconds path:(NSString *)path {
    return jfx_mobile_player_write_graph(_player,UINT32_MAX,seconds,path.UTF8String)==JFX_SUCCESS;
}

@end
