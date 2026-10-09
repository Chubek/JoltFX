#import "JFXModeling3DViewController.h"
#include <math.h>
#include <stdint.h>
@interface JFXModeling3DViewController () <UIGestureRecognizerDelegate>
@property(nonatomic,strong) JFXMobilePlayerBridge *player;
@property(nonatomic,strong) UIStackView *content;
@property(nonatomic,strong) UIImageView *preview;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) UISlider *scrubber;
@property(nonatomic,strong) CADisplayLink *clock;
@property(nonatomic) NSUInteger selected;
@property(nonatomic) double seconds;
@property(nonatomic) CFTimeInterval previous;
@property(nonatomic) BOOL navigating;
@property(nonatomic) NSUInteger navigationGestures;
@property(nonatomic) BOOL navigationCancelled;
@end
@implementation JFXModeling3DViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player {
    if ((self=[super init])) _player=player; return self;
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"3D Modeling & Animation";
    UIScrollView *scroll=[[UIScrollView alloc] initWithFrame:self.view.bounds];
    scroll.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight; [self.view addSubview:scroll];
    self.content=[[UIStackView alloc] init]; self.content.axis=UILayoutConstraintAxisVertical; self.content.spacing=8;
    self.content.translatesAutoresizingMaskIntoConstraints=NO; [scroll addSubview:self.content];
    [NSLayoutConstraint activateConstraints:@[
        [self.content.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],
        [self.content.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],
        [self.content.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
        [self.content.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
        [self.content.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]]];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(stop) name:UIApplicationWillResignActiveNotification object:nil];
}
- (void)viewWillAppear:(BOOL)animated {
    [super viewWillAppear:animated]; [self.player stopAudio];
    [self.player edit:@"3d" track:0 clip:0 target:0 value:0 text:@""]; [self refresh];
}
- (void)stop { [self.clock invalidate]; self.clock=nil; self.previous=0; }
- (void)viewDidDisappear:(BOOL)animated {
    [super viewDidDisappear:animated]; [self stop];
    if (self.navigating) { [self.player edit:@"3d.navigation_cancel" track:0 clip:0 target:0 value:0 text:@""]; self.navigating=NO; self.navigationGestures=0; }
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (UIButton *)button:(NSString *)title action:(void (^)(void))action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem]; [b setTitle:title forState:UIControlStateNormal];
    [b addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) { (void)sender; action(); }] forControlEvents:UIControlEventTouchUpInside];
    [self.content addArrangedSubview:b]; return b;
}
- (UITextField *)field:(NSString *)title value:(NSString *)value {
    UILabel *label=[[UILabel alloc] init]; label.text=title; [self.content addArrangedSubview:label];
    UITextField *input=[[UITextField alloc] init]; input.text=value; input.accessibilityLabel=title; input.borderStyle=UITextBorderStyleRoundedRect;
    [self.content addArrangedSubview:input]; return input;
}
- (double)number:(UITextField *)input {
    double value; NSScanner *scanner=[NSScanner scannerWithString:input.text ?: @""];
    return [scanner scanDouble:&value] && scanner.isAtEnd?value:NAN;
}
- (void)apply:(NSString *)op a:(NSUInteger)a b:(NSUInteger)b c:(NSUInteger)c value:(double)value text:(NSString *)text {
    if (![self.player edit:op track:a clip:b target:c value:value text:text]) { self.status.text=@"3D edit rejected. Check object, frame and value ranges."; return; }
    if ([op isEqualToString:@"3d.new"]) { [self stop]; self.seconds=0; }
    [self refresh];
}
- (void)showPreview {
    [self.player seekSeconds:self.seconds]; NSData *pixels=[self.player previewRGBA]; if (!pixels) { self.status.text=@"Unable to render 3D scene"; return; }
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels); CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(320,180,8,32,1280,space,kCGBitmapByteOrderDefault|kCGImageAlphaLast,provider,NULL,NO,kCGRenderingIntentDefault);
    self.preview.image=[UIImage imageWithCGImage:image]; CGImageRelease(image); CGColorSpaceRelease(space); CGDataProviderRelease(provider);
}
- (BOOL)navigationGesture:(UIGestureRecognizer *)gesture {
    if (gesture.state==UIGestureRecognizerStateBegan) {
        if (!self.navigationGestures) { self.navigationCancelled=NO; self.navigating=[self.player edit:@"3d.navigation_begin" track:0 clip:0 target:0 value:0 text:@""]; }
        ++self.navigationGestures;
    }
    if (gesture.state==UIGestureRecognizerStateEnded || gesture.state==UIGestureRecognizerStateCancelled || gesture.state==UIGestureRecognizerStateFailed) {
        self.navigationCancelled|=gesture.state!=UIGestureRecognizerStateEnded;
        if (self.navigationGestures) --self.navigationGestures;
        if (!self.navigationGestures && self.navigating) {
            [self.player edit:self.navigationCancelled?@"3d.navigation_cancel":@"3d.navigation_end" track:0 clip:0 target:0 value:0 text:@""];
            self.navigating=NO; [self refresh];
        }
        return NO;
    }
    return self.navigating;
}
- (BOOL)gestureRecognizer:(UIGestureRecognizer *)gesture shouldRecognizeSimultaneouslyWithGestureRecognizer:(UIGestureRecognizer *)other {
    return gesture.view==self.preview && other.view==self.preview;
}
- (void)orbitGesture:(UIPanGestureRecognizer *)gesture {
    if (![self navigationGesture:gesture]) return;
    CGPoint delta=[gesture translationInView:self.preview]; [gesture setTranslation:CGPointZero inView:self.preview];
    NSString *op=@"3d.orbit",*text=[NSString stringWithFormat:@"%.9g %.9g 0",-delta.x*.4,-delta.y*.4];
    if (gesture.numberOfTouches>=2) {
        NSArray *camera=[self.player scene3DState][@"camera"];
        double scale=2*[camera[2] doubleValue]*tan([camera[6] doubleValue]*M_PI/360)/MAX(1,self.preview.bounds.size.height);
        op=@"3d.pan"; text=[NSString stringWithFormat:@"%.9g %.9g 0",-delta.x*scale,delta.y*scale];
    }
    [self.player edit:op track:0 clip:0 target:0 value:0 text:text]; [self showPreview];
}
- (void)zoomGesture:(UIPinchGestureRecognizer *)gesture {
    if (![self navigationGesture:gesture]) return;
    [self.player edit:@"3d.dolly" track:0 clip:0 target:0 value:MAX(-10,MIN(10,log(MAX(.0001,gesture.scale)))) text:@""]; gesture.scale=1; [self showPreview];
}
- (void)rollGesture:(UIRotationGestureRecognizer *)gesture {
    if (![self navigationGesture:gesture]) return;
    [self.player edit:@"3d.orbit" track:0 clip:0 target:0 value:0 text:[NSString stringWithFormat:@"0 0 %.9g",gesture.rotation*180/M_PI]]; gesture.rotation=0; [self showPreview];
}
- (void)tick:(CADisplayLink *)link {
    NSDictionary *state=[self.player scene3DState]; double fps=[state[@"fps"] doubleValue],duration=[state[@"frames"] doubleValue]/MAX(1,fps);
    if (self.previous) self.seconds+=link.timestamp-self.previous; self.previous=link.timestamp;
    if (duration>0) self.seconds=fmod(self.seconds,duration);
    self.scrubber.value=(float)(self.seconds*fps); [self showPreview];
}
- (void)refresh {
    for (UIView *v in self.content.arrangedSubviews) { [self.content removeArrangedSubview:v]; [v removeFromSuperview]; }
    NSDictionary *state=[self.player scene3DState]; NSArray *objects=state[@"objects"] ?: @[];
    double fps=MAX(1,[state[@"fps"] doubleValue]);
    self.seconds=MIN(self.seconds,MAX(0,[state[@"frames"] doubleValue]-1)/fps);
    if (self.selected>=objects.count) self.selected=0;
    self.preview=[[UIImageView alloc] init]; self.preview.contentMode=UIViewContentModeScaleAspectFit;
    self.preview.userInteractionEnabled=YES;
    [self.preview addGestureRecognizer:[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(orbitGesture:)]];
    [self.preview addGestureRecognizer:[[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(zoomGesture:)]];
    [self.preview addGestureRecognizer:[[UIRotationGestureRecognizer alloc] initWithTarget:self action:@selector(rollGesture:)]];
    for (UIGestureRecognizer *gesture in self.preview.gestureRecognizers) gesture.delegate=self;
    [self.preview.heightAnchor constraintEqualToConstant:180].active=YES; [self.content addArrangedSubview:self.preview];
    self.status=[[UILabel alloc] init]; self.status.numberOfLines=0; [self.content addArrangedSubview:self.status];
    __weak JFXModeling3DViewController *weak=self;
    [self button:@"Play / pause 3D" action:^{
        if (weak.clock) [weak stop]; else { weak.previous=0; weak.clock=[CADisplayLink displayLinkWithTarget:weak selector:@selector(tick:)]; [weak.clock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes]; }
    }];
    self.scrubber=[[UISlider alloc] init]; self.scrubber.maximumValue=MAX(0,[state[@"frames"] floatValue]-1); self.scrubber.value=(float)(self.seconds*[state[@"fps"] doubleValue]);
    [self.scrubber addAction:[UIAction actionWithHandler:^(__kindof UIAction *action) { (void)action; weak.seconds=weak.scrubber.value/MAX(1,[state[@"fps"] doubleValue]); [weak showPreview]; }] forControlEvents:UIControlEventValueChanged];
    [self.content addArrangedSubview:self.scrubber];
    UITextField *clockFPS=[self field:@"3D FPS" value:[state[@"fps"] stringValue]],*clockFrames=[self field:@"3D duration frames" value:[state[@"frames"] stringValue]];
    [self button:@"Set 3D clock" action:^{ double f=[weak number:clockFPS],n=[weak number:clockFrames];
        if (isfinite(f) && f>=1 && f<=240 && floor(f)==f && isfinite(n) && n>=1 && n<=36000 && floor(n)==n)
            [weak apply:@"3d.clock" a:(NSUInteger)f b:(NSUInteger)n c:0 value:0 text:@""]; }];
    for (NSString *op in @[@"undo",@"redo",@"3d.new"]) [self button:op action:^{ [weak apply:op a:0 b:0 c:0 value:0 text:@""]; }];
    UITextField *segments=[self field:@"Primitive segments (8..128)" value:@"64"];
    for (NSString *primitive in @[@"cube",@"sphere",@"plane",@"cylinder",@"cone",@"torus",@"capsule",@"pyramid",@"disk",@"nurbs",@"metaball",
        @"tube",@"hemisphere",@"wedge",@"tetrahedron",@"octahedron",@"icosahedron"]) [self button:[@"Add " stringByAppendingString:primitive] action:^{
        double n=[weak number:segments]; if (isfinite(n) && n>=8 && n<=128 && floor(n)==n) [weak apply:@"3d.add" a:(NSUInteger)n b:0 c:0 value:0 text:primitive]; }];
    [objects enumerateObjectsUsingBlock:^(NSDictionary *o,NSUInteger i,BOOL *stop) { (void)stop;
        [weak button:[NSString stringWithFormat:@"%lu: %@ (%@ vertices)",(unsigned long)i,o[@"name"],o[@"vertices"]] action:^{ weak.selected=i; [weak refresh]; }]; }];
    if (objects.count) {
        NSDictionary *o=objects[self.selected]; NSArray *transforms=o[@"transform"],*names=@[@"Position X",@"Position Y",@"Position Z",@"Rotation X",@"Rotation Y",@"Rotation Z",@"Scale X",@"Scale Y",@"Scale Z"];
        [self button:[o[@"smooth"] boolValue]?@"Flat shading":@"Smooth shading" action:^{ [weak apply:@"3d.smooth" a:weak.selected b:0 c:0 value:[o[@"smooth"] boolValue]?0:1 text:@""]; }];
        NSUInteger generator=[o[@"generator"] unsignedIntegerValue];
        if (generator) {
            NSArray *points=o[generator==1?@"controls":@"balls"];
            [points enumerateObjectsUsingBlock:^(NSArray *point,NSUInteger i,BOOL *stop) { (void)stop;
                for (NSUInteger ch=0;ch<4;++ch) {
                    UITextField *value=[weak field:[NSString stringWithFormat:@"%@ %lu / %@",generator==1?@"Control":@"Ball",(unsigned long)i,@[@"X",@"Y",@"Z",generator==1?@"Weight":@"Radius"][ch]] value:[point[ch] stringValue]];
                    [weak button:@"Apply generator component" action:^{ [weak apply:generator==1?@"3d.nurbs_point":@"3d.metaball_point" a:weak.selected b:i c:ch value:[weak number:value] text:@""]; }];
                }
                if (generator==2) [weak button:[NSString stringWithFormat:@"Remove ball %lu",(unsigned long)i] action:^{ [weak apply:@"3d.metaball_remove" a:weak.selected b:i c:0 value:0 text:@""]; }];
            }];
            UITextField *resolution=[self field:@"Surface resolution (8..64)" value:[o[@"resolution"] stringValue]];
            [self button:@"Set surface resolution" action:^{ double n=[weak number:resolution]; if (isfinite(n) && n>=8 && n<=64 && floor(n)==n) [weak apply:@"3d.resolution" a:weak.selected b:(NSUInteger)n c:0 value:0 text:@""]; }];
            if (generator==2) {
                UITextField *ball=[self field:@"New metaball X Y Z radius" value:@"0 1 0 1"];
                [self button:@"Add metaball" action:^{ [weak apply:@"3d.metaball_add" a:weak.selected b:0 c:0 value:0 text:ball.text ?: @""]; }];
            }
            [self button:@"Make generator editable" action:^{ [weak apply:@"3d.make_editable" a:weak.selected b:0 c:0 value:0 text:@""]; }];
        }
        NSDictionary *cloner=o[@"cloner"];
        UITextField *cloneMode=[self field:@"Cloner (0 off, 1 linear, 2 radial, 3 grid)" value:[cloner[@"mode"] stringValue]],*cloneCount=[self field:@"Clone count (1..64)" value:[cloner[@"count"] stringValue]],*spacing=[self field:@"Clone spacing / radius" value:[cloner[@"spacing"] stringValue]];
        [self button:@"Apply cloner" action:^{ double mode=[weak number:cloneMode],count=[weak number:cloneCount];
            if (isfinite(mode) && mode>=0 && mode<=3 && floor(mode)==mode && isfinite(count) && count>=1 && count<=64 && floor(count)==count)
                [weak apply:@"3d.cloner" a:weak.selected b:(NSUInteger)mode c:(NSUInteger)count value:[weak number:spacing] text:@""]; }];
        [self button:@"Make clones real at playhead" action:^{ [weak apply:@"3d.cloner_make_real" a:weak.selected b:0 c:0 value:weak.seconds text:@""]; }];
        UITextField *scriptChannel=[self field:@"Joltscript channel (0..8)" value:@"4"];
        UITextField *script=[self field:@"Animation script: time frame index value" value:@"(defkernel spin [time frame index value] (+ value (* time 90)))"];
        [self button:@"Load assigned animation script" action:^{ double ch=[weak number:scriptChannel]; if (isfinite(ch) && ch>=0 && ch<=8 && floor(ch)==ch) script.text=o[@"scripts"][(NSUInteger)ch]; }];
        [self button:@"Apply animation script" action:^{ double ch=[weak number:scriptChannel]; if (isfinite(ch) && ch>=0 && ch<=8 && floor(ch)==ch) [weak apply:@"3d.script" a:weak.selected b:(NSUInteger)ch c:0 value:0 text:script.text ?: @""]; }];
        [self button:@"Remove animation script" action:^{ double ch=[weak number:scriptChannel]; if (isfinite(ch) && ch>=0 && ch<=8 && floor(ch)==ch) [weak apply:@"3d.script" a:weak.selected b:(NSUInteger)ch c:0 value:0 text:@""]; }];
        NSString *scriptsDirectory=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
        UITextField *scriptPath=[self field:@"Animation .jolt path" value:[scriptsDirectory stringByAppendingPathComponent:@"spin.jolt"]];
        [self button:@"Load animation .jolt" action:^{ double ch=[weak number:scriptChannel]; if (isfinite(ch) && ch>=0 && ch<=8 && floor(ch)==ch) [weak apply:@"3d.script_file" a:weak.selected b:(NSUInteger)ch c:0 value:0 text:scriptPath.text ?: @""]; }];
        UITextField *name=[self field:@"3D object name" value:o[@"name"]];
        [self button:@"Rename 3D object" action:^{ [weak apply:@"3d.name" a:weak.selected b:0 c:0 value:0 text:name.text ?: @""]; }];
        [self button:[o[@"visible"] boolValue]?@"Hide mesh":@"Show mesh" action:^{ [weak apply:@"3d.visible" a:weak.selected b:0 c:0 value:[o[@"visible"] boolValue]?0:1 text:@""]; }];
        NSArray *colors=o[@"color"],*colorNames=@[@"Material R",@"Material G",@"Material B"];
        for (NSUInteger ch=0;ch<3;++ch) { UITextField *input=[self field:colorNames[ch] value:[colors[ch] stringValue]];
            [self button:[@"Apply " stringByAppendingString:colorNames[ch]] action:^{ [weak apply:@"3d.color" a:weak.selected b:ch c:0 value:[weak number:input] text:@""]; }]; }
        for (NSUInteger ch=0;ch<9;++ch) {
            UITextField *input=[self field:names[ch] value:[transforms[ch] stringValue]];
            [self button:[@"Apply " stringByAppendingString:names[ch]] action:^{ [weak apply:@"3d.transform" a:weak.selected b:ch c:0 value:[weak number:input] text:@""]; }];
            [self button:[@"Key " stringByAppendingString:names[ch]] action:^{ [weak apply:@"3d.key" a:weak.selected b:ch c:(NSUInteger)weak.scrubber.value value:[weak number:input] text:@""]; }];
            [self button:[@"Remove key " stringByAppendingString:names[ch]] action:^{ [weak apply:@"3d.key_remove" a:weak.selected b:ch c:(NSUInteger)weak.scrubber.value value:0 text:@""]; }];
        }
        for (NSString *op in @[@"3d.duplicate",@"3d.remove",@"3d.subdivide",@"3d.align"]) [self button:op action:^{ [weak apply:op a:weak.selected b:0 c:0 value:0 text:@""]; }];
        UITextField *channel=[self field:@"Key channel (0..8)" value:@"0"],*curve=[self field:@"Interpolation (0 hold, 1 linear, 2 smooth)" value:@"1"];
        [self button:@"Set key interpolation" action:^{ double ch=[weak number:channel];
            if (isfinite(ch) && ch>=0 && ch<=8 && floor(ch)==ch) [weak apply:@"3d.interpolation" a:weak.selected b:(NSUInteger)ch c:(NSUInteger)weak.scrubber.value value:[weak number:curve] text:@""]; }];
        UITextField *mass=[self field:@"Rigid-body mass (0 = static)" value:[o[@"mass"] stringValue]];
        [self button:@"Set mass" action:^{ [weak apply:@"3d.mass" a:weak.selected b:0 c:0 value:[weak number:mass] text:@""]; }];
        UITextField *vertex=[self field:@"Vertex index" value:@"0"],*axis=[self field:@"Vertex axis (0..2)" value:@"0"],*coordinate=[self field:@"Vertex coordinate" value:@"0"];
        [self button:@"Set vertex coordinate" action:^{ double v=[weak number:vertex],a=[weak number:axis];
            if (!isfinite(v) || v<0 || floor(v)!=v || v>65535 || !isfinite(a) || a<0 || a>2 || floor(a)!=a) return;
            [weak apply:@"3d.vertex" a:weak.selected b:(NSUInteger)v c:(NSUInteger)a value:[weak number:coordinate] text:@""]; }];
    }
    NSArray *camera=state[@"camera"],*cameraNames=@[@"Orbit yaw",@"Orbit pitch",@"Camera distance",@"Target X",@"Target Y",@"Target Z",@"Field of view"];
    for (NSUInteger i=0;i<camera.count;++i) { UITextField *input=[self field:cameraNames[i] value:[camera[i] stringValue]];
        [self button:[@"Apply " stringByAppendingString:cameraNames[i]] action:^{ [weak apply:@"3d.camera" a:i b:0 c:0 value:[weak number:input] text:@""]; }]; }
    for (NSUInteger i=0;i<6;++i) [self button:[@"3D " stringByAppendingString:@[@"Front",@"Right",@"Top",@"Back",@"Left",@"Bottom"][i]] action:^{ [weak apply:@"3d.view" a:i b:0 c:0 value:0 text:@""]; }];
    for (NSUInteger i=0;i<3;++i) [self button:[@"Gimbal " stringByAppendingString:@[@"X",@"Y",@"Z"][i]] action:^{ [weak apply:@"3d.orbit_axis" a:0 b:0 c:0 value:15 text:@[@"1 0 0",@"0 1 0",@"0 0 1"][i]]; }];
    UITextField *bake=[self field:@"Physics bake frames (1..600)" value:@"60"];
    [self button:@"Bake rigid bodies" action:^{ double n=[weak number:bake]; if (isfinite(n) && n>=1 && n<=600 && floor(n)==n) [weak apply:@"3d.bake" a:0 b:0 c:(NSUInteger)n value:0 text:@""]; }];
    NSString *directory=NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,NSUserDomainMask,YES).firstObject;
    UITextField *mesh=[self field:@"PLY path" value:[directory stringByAppendingPathComponent:@"mesh.ply"]];
    for (NSString *op in @[@"3d.import_ply",@"3d.export_ply"]) [self button:op action:^{ [weak apply:op a:weak.selected b:0 c:0 value:0 text:mesh.text ?: @""]; }];
    UITextField *path=[self field:@"3D project path" value:[directory stringByAppendingPathComponent:@"scene3d.jfx"]];
    [self button:@"Save 3D project" action:^{ NSString *doc=[weak.player saveDocument]; weak.status.text=doc && [doc writeToFile:path.text atomically:YES encoding:NSUTF8StringEncoding error:nil]?@"Saved":@"Save failed"; }];
    [self button:@"Open 3D project" action:^{ NSString *doc=[NSString stringWithContentsOfFile:path.text encoding:NSUTF8StringEncoding error:nil];
        if (doc && [weak.player loadScene3D:doc]) { [weak stop]; weak.seconds=0; [weak refresh]; } else weak.status.text=@"Unable to open 3D scene"; }];
    [self button:@"Export frame (PPM)" action:^{ weak.status.text=[weak.player writeFrame:(NSUInteger)weak.scrubber.value path:[directory stringByAppendingPathComponent:@"scene3d.ppm"]]?@"Exported":@"Export failed"; }];
    [self showPreview];
}
@end
