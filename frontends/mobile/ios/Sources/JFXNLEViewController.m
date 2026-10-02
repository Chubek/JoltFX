#import "JFXNLEViewController.h"
#import "JFXColorViewController.h"
#import "JFXCompositionViewController.h"
#include <math.h>
#include <stdint.h>

@interface JFXNLEViewController ()
@property(nonatomic,strong) JFXMobilePlayerBridge *player;
@property(nonatomic,strong) UIStackView *content;
@property(nonatomic,strong) UIImageView *preview;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UISlider *scrubber;
@property(nonatomic,strong) CADisplayLink *clock;
@property(nonatomic,strong) CADisplayLink *exportClock;
@property(nonatomic) CFTimeInterval previous;
@property(nonatomic) double fps;
@property(nonatomic) NSUInteger duration;
@property(nonatomic) double position;
@end
@implementation JFXNLEViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player {
    if ((self=[super init])) _player=player;
    return self;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"Non-Linear Editor";
    UIScrollView *scroll=[[UIScrollView alloc] initWithFrame:self.view.bounds];
    scroll.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:scroll];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(stopPlayback) name:UIApplicationWillResignActiveNotification object:nil];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(cancelExport) name:UIApplicationWillResignActiveNotification object:nil];
    self.content=[[UIStackView alloc] init]; self.content.axis=UILayoutConstraintAxisVertical;
    self.content.spacing=8; self.content.translatesAutoresizingMaskIntoConstraints=NO; [scroll addSubview:self.content];
    [NSLayoutConstraint activateConstraints:@[
        [self.content.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],
        [self.content.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],
        [self.content.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
        [self.content.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
        [self.content.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]]];
    [self refresh];
}
- (void)stopPlayback { [self.clock invalidate]; self.clock=nil; self.previous=0; [self.player stopAudio]; }
- (void)cancelExport { [self.exportClock invalidate]; self.exportClock=nil; [self.player cancelVideoExport]; }
- (void)viewDidDisappear:(BOOL)animated { [super viewDidDisappear:animated]; [self stopPlayback]; [self cancelExport]; }
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (void)viewWillAppear:(BOOL)animated { [super viewWillAppear:animated]; [self.player edit:@"sequence" track:0 clip:0 target:0 value:0 text:@""]; [self refresh]; }
- (UIButton *)button:(NSString *)title action:(void (^)(void))action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem]; [b setTitle:title forState:UIControlStateNormal];
    [b addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) { (void)sender; action(); }] forControlEvents:UIControlEventTouchUpInside];
    [self.content addArrangedSubview:b]; return b;
}
- (UITextField *)field:(NSString *)title value:(NSString *)value {
    UILabel *label=[[UILabel alloc] init]; label.text=title; [self.content addArrangedSubview:label];
    UITextField *field=[[UITextField alloc] init]; field.text=value; field.borderStyle=UITextBorderStyleRoundedRect;
    field.accessibilityLabel=title; [self.content addArrangedSubview:field]; return field;
}
- (double)number:(UITextField *)field {
    double value; NSScanner *scanner=[NSScanner scannerWithString:field.text ?: @""];
    return [scanner scanDouble:&value] && scanner.isAtEnd?value:NAN;
}
- (void)apply:(NSString *)op target:(double)target value:(double)value text:(NSString *)text {
    if (!isfinite(target) || target<0 || floor(target)!=target || target>UINT32_MAX ||
        ![self.player edit:op track:self.track clip:self.clip target:(NSUInteger)target value:value text:text]) {
        self.status.text=@"Edit rejected. Check frame ranges, selection and source handles."; return;
    }
    [self refresh];
}
- (void)showPreview {
    if (![self.player seekSeconds:self.frame/self.fps]) { self.status.text=@"Unable to seek to the selected frame."; return; }
    NSData *pixels=[self.player previewRGBA];
    if (!pixels) { self.status.text=@"Unable to render the selected frame."; return; }
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels);
    CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(320,180,8,32,1280,space,kCGBitmapByteOrderDefault|kCGImageAlphaLast,provider,NULL,NO,kCGRenderingIntentDefault);
    self.preview.image=[UIImage imageWithCGImage:image];
    CGImageRelease(image); CGColorSpaceRelease(space); CGDataProviderRelease(provider);
}
- (void)tick:(CADisplayLink *)link {
    if (self.previous) self.position+=(link.timestamp-self.previous)*self.fps;
    else self.position=self.frame;
    self.previous=link.timestamp;
    if (self.duration) self.position=fmod(self.position,self.duration); else self.position=0;
    self.frame=(NSUInteger)self.position;
    self.scrubber.value=(float)self.frame; [self showPreview];
    if (![self.player pumpAudioAtSeconds:self.position/self.fps]) { [self stopPlayback]; self.status.text=@"Unable to mix or play audio. Check media resources."; }
}
- (void)exportTick:(CADisplayLink *)link {
    (void)link; NSDictionary *progress=[self.player stepVideoExport];
    self.status.text=[progress[@"result"] intValue]==0?[NSString stringWithFormat:@"Video export: %@ frames",progress[@"frames"]]:@"Video export failed. Check resources, codecs and output path.";
    if ([progress[@"state"] intValue]!=0) { [self.exportClock invalidate]; self.exportClock=nil; }
}
- (void)refresh {
    if (!self.isViewLoaded) return;
    [self.player stopAudio];
    for (UIView *v in self.content.arrangedSubviews) { [self.content removeArrangedSubview:v]; [v removeFromSuperview]; }
    NSDictionary *state=[self.player sequenceState]; NSArray *tracks=state[@"tracks"] ?: @[];
    self.fps=[state[@"fpsNum"] doubleValue]/MAX(1,[state[@"fpsDen"] doubleValue]); if (self.fps<=0) self.fps=30;
    self.duration=[state[@"duration"] unsignedIntegerValue];
    if (self.frame>=self.duration) self.frame=self.duration?self.duration-1:0;
    self.position=self.frame; self.previous=0;
    if (self.track>=tracks.count) self.track=0;
    NSArray *clips=tracks.count?tracks[self.track][@"clips"]:@[];
    if (self.clip>=clips.count) self.clip=0;
    NSDictionary *selected=clips.count?clips[self.clip]:@{};
    self.preview=[[UIImageView alloc] init]; self.preview.contentMode=UIViewContentModeScaleAspectFit;
    [self.preview.heightAnchor constraintEqualToConstant:180].active=YES; [self.content addArrangedSubview:self.preview];
    self.status=[[UILabel alloc] init]; self.status.numberOfLines=0; [self.content addArrangedSubview:self.status];
    __weak JFXNLEViewController *weak=self;
    [self button:@"Play / pause" action:^{
        if (weak.clock) [weak stopPlayback];
        else { weak.previous=0; weak.clock=[CADisplayLink displayLinkWithTarget:weak selector:@selector(tick:)]; weak.clock.preferredFramesPerSecond=30; [weak.clock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes]; }
    }];
    self.scrubber=[[UISlider alloc] init]; self.scrubber.maximumValue=(float)MAX(1,self.duration)-1; self.scrubber.value=(float)self.frame;
    [self.scrubber addAction:[UIAction actionWithHandler:^(__kindof UIAction *action) {
        (void)action; [weak.player stopAudio]; weak.frame=(NSUInteger)weak.scrubber.value; weak.position=weak.frame; [weak showPreview];
    }] forControlEvents:UIControlEventValueChanged]; [self.content addArrangedSubview:self.scrubber];
    [self button:@"Undo" action:^{ [weak apply:@"undo" target:0 value:0 text:@""]; }].enabled=[state[@"canUndo"] boolValue];
    [self button:@"Redo" action:^{ [weak apply:@"redo" target:0 value:0 text:@""]; }].enabled=[state[@"canRedo"] boolValue];
    [tracks enumerateObjectsUsingBlock:^(NSDictionary *track,NSUInteger ti,BOOL *stop) {
        (void)stop; [weak button:[NSString stringWithFormat:@"%@Track %lu: %@%@%@",ti==weak.track?@"▶ ":@"",(unsigned long)ti,track[@"name"],[track[@"muted"] boolValue]?@" (muted)":@"",[track[@"solo"] boolValue]?@" (solo)":@""] action:^{ weak.track=ti; weak.clip=0; [weak refresh]; }];
        [track[@"clips"] enumerateObjectsUsingBlock:^(NSDictionary *clip,NSUInteger ci,BOOL *done) {
            (void)done; [weak button:[NSString stringWithFormat:@"%@: frame %@, length %@, source in %@",clip[@"name"],clip[@"start"],clip[@"length"],clip[@"inPoint"]] action:^{ weak.track=ti; weak.clip=ci; weak.frame=[clip[@"start"] unsignedIntegerValue]; [weak refresh]; }];
        }];
    }];
    UITextField *start=[self field:@"Start frame" value:[selected[@"start"] stringValue] ?: @"0"];
    UITextField *length=[self field:@"Length (frames)" value:[selected[@"length"] stringValue] ?: @"90"];
    UITextField *target=[self field:@"Destination track" value:[NSString stringWithFormat:@"%lu",(unsigned long)self.track]];
    UITextField *slip=[self field:@"Slip delta (frames)" value:@"0"];
    UITextField *name=[self field:@"Name" value:selected[@"name"] ?: @"Video"];
    UITextField *media=[self field:@"Media path" value:@""];
    [self button:@"Trim" action:^{ [weak apply:@"clip.trim" target:[weak number:start] value:[weak number:length] text:@""]; }];
    [self button:@"Move" action:^{ [weak apply:@"clip.move" target:[weak number:target] value:[weak number:start] text:@""]; }];
    [self button:@"Duplicate" action:^{ [weak apply:@"clip.duplicate" target:[weak number:target] value:[weak number:start] text:@""]; }];
    [self button:@"Slip source" action:^{ [weak apply:@"clip.slip" target:0 value:[weak number:slip] text:@""]; }];
    [self button:@"Rename clip" action:^{ [weak apply:@"clip.name" target:0 value:0 text:name.text ?: @""]; }];
    [self button:@"Split at playhead" action:^{ [weak apply:@"clip.split" target:0 value:weak.frame text:@""]; }];
    for (NSString *op in @[@"clip.remove",@"clip.ripple_delete"]) [self button:op action:^{ [weak apply:op target:0 value:0 text:@""]; }];
    [self button:@"Insert gap" action:^{ [weak apply:@"track.insert_gap" target:[weak number:start] value:[weak number:length] text:@""]; }];
    [self button:@"Add track" action:^{ [weak apply:@"track.add" target:0 value:0 text:name.text ?: @"Video"]; }];
    [self button:@"Rename track" action:^{ [weak apply:@"track.name" target:0 value:0 text:name.text ?: @""]; }];
    [self button:@"Move track" action:^{ [weak apply:@"track.move" target:0 value:[weak number:target] text:@""]; }];
    [self button:@"Remove track" action:^{ [weak apply:@"track.remove" target:0 value:0 text:@""]; }];
    NSDictionary *track=tracks.count?tracks[self.track]:@{};
    [self button:@"Mute / unmute track" action:^{ [weak apply:@"track.mute" target:0 value:![track[@"muted"] boolValue] text:@""]; }];
    [self button:@"Solo / unsolo track" action:^{ [weak apply:@"track.solo" target:0 value:![track[@"solo"] boolValue] text:@""]; }];
    NSDictionary *audio=selected[@"audio"] ?: @{};
    for (NSDictionary *control in @[@{@"label":@"Clip audio gain (0..16)",@"op":@"clip.audio.gain",@"value":audio[@"gain"] ?: @1},
        @{@"label":@"Stereo pan (-1..1)",@"op":@"clip.audio.pan",@"value":audio[@"pan"] ?: @0},
        @{@"label":@"Audio fade in frames",@"op":@"clip.audio.fade_in",@"value":audio[@"fadeIn"] ?: @0},
        @{@"label":@"Audio fade out frames",@"op":@"clip.audio.fade_out",@"value":audio[@"fadeOut"] ?: @0},
        @{@"label":@"Track audio gain (0..16)",@"op":@"track.audio.gain",@"value":track[@"audioGain"] ?: @1}]) {
        UITextField *field=[self field:control[@"label"] value:[control[@"value"] stringValue]];
        [self button:[@"Apply " stringByAppendingString:control[@"label"]] action:^{ [weak apply:control[@"op"] target:0 value:[weak number:field] text:@""]; }];
    }
    [self button:@"Enable / mute clip audio" action:^{ [weak apply:@"clip.audio.enabled" target:0 value:![audio[@"enabled"] boolValue] text:@""]; }];
    for (NSDictionary *source in @[@{@"name":@"Solid",@"id":@0},@{@"name":@"Image",@"id":@4},@{@"name":@"Video",@"id":@5},@{@"name":@"Audio",@"id":@6}])
        [self button:[@"Add " stringByAppendingString:source[@"name"]] action:^{
            if ([weak.player edit:@"clip.add" track:weak.track clip:[source[@"id"] unsignedIntegerValue] target:weak.frame value:[weak number:length] text:media.text ?: @""]) [weak refresh];
            else weak.status.text=@"Unable to add clip. Check media path and duration.";
        }];
    [self button:@"Color Calibration / Color Grading" action:^{
        JFXColorViewController *colors=[[JFXColorViewController alloc] initWithPlayer:weak.player]; colors.track=weak.track; colors.clip=weak.clip;
        [weak presentViewController:colors animated:YES completion:nil];
    }];
    [self button:@"Node Composition" action:^{
        [weak stopPlayback]; JFXCompositionViewController *nodes=[[JFXCompositionViewController alloc] initWithPlayer:weak.player];
        nodes.modalPresentationStyle=UIModalPresentationFullScreen;
        [weak presentViewController:nodes animated:YES completion:nil];
    }];
    UITextField *file=[self field:@"Project file path" value:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/project.jfx"]];
    [self button:@"Open project" action:^{
        NSString *doc=[NSString stringWithContentsOfFile:file.text encoding:NSUTF8StringEncoding error:nil];
        if (doc && [weak.player loadDocument:doc]) [weak refresh]; else weak.status.text=@"Unable to open project.";
    }];
    [self button:@"Save project" action:^{
        NSString *doc=[weak.player saveDocument];
        if (![doc writeToFile:file.text atomically:YES encoding:NSUTF8StringEncoding error:nil]) weak.status.text=@"Unable to save project.";
    }];
    UITextField *output=[self field:@"Frame export path (PPM)" value:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/frame.ppm"]];
    [self button:@"Export selected frame" action:^{
        if (![weak.player writeFrame:weak.frame path:output.text ?: @""]) weak.status.text=@"Unable to export frame.";
    }];
    UITextField *video=[self field:@"Encoded video path (.mp4/.mov/.mkv)" value:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/sequence.mp4"]];
    UITextField *exportStart=[self field:@"Video start frame" value:@"0"];
    UITextField *exportFrames=[self field:@"Video frame count (0: sequence)" value:@"0"];
    for (NSNumber *includeAudio in @[@YES,@NO]) [self button:includeAudio.boolValue?@"Export video with mixed audio":@"Export silent video" action:^{
        double startFrame=[weak number:exportStart],count=[weak number:exportFrames];
        if (!isfinite(startFrame) || !isfinite(count) || startFrame<0 || count<0 || startFrame>1.e7 || count>1.e7 || floor(startFrame)!=startFrame || floor(count)!=count ||
            ![weak.player beginVideoExport:video.text ?: @"" startFrame:(NSUInteger)startFrame frameCount:(NSUInteger)count audio:includeAudio.boolValue]) {
            weak.status.text=@"Unable to begin export. Check codec availability, frame range and output path."; return;
        }
        weak.exportClock=[CADisplayLink displayLinkWithTarget:weak selector:@selector(exportTick:)];
        [weak.exportClock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }];
    [self button:@"Cancel video export" action:^{ [weak cancelExport]; }];
    UITextField *width=[self field:@"New sequence width" value:@"320"];
    UITextField *height=[self field:@"New sequence height" value:@"180"];
    UITextField *fpsNum=[self field:@"FPS numerator" value:@"30"];
    UITextField *fpsDen=[self field:@"FPS denominator" value:@"1"];
    [self button:@"New empty sequence" action:^{
        double w=[weak number:width],h=[weak number:height],n=[weak number:fpsNum],d=[weak number:fpsDen];
        if (w>=1 && w<=4096 && h>=1 && h<=4096 && n>=1 && n<=1.e9 && floor(w)==w && floor(h)==h && floor(n)==n &&
            [weak.player edit:@"sequence.new" track:(NSUInteger)w clip:(NSUInteger)h target:(NSUInteger)n value:d text:@""]) {
            weak.frame=0; weak.position=0; [weak refresh];
        } else weak.status.text=@"Invalid raster or frame rate.";
    }];
    [self showPreview];
}
@end
