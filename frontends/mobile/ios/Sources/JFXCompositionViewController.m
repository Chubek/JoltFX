#import "JFXCompositionViewController.h"
#include <math.h>
#include <stdint.h>

@interface JFXCompositionCanvas : UIView
@property(nonatomic,copy) NSDictionary *state;
@property(nonatomic,copy) NSArray<NSDictionary *> *catalog;
@property(nonatomic) NSUInteger selected;
@property(nonatomic) CGFloat zoom;
@property(nonatomic) CGPoint pan;
@property(nonatomic) NSInteger dragging, wiring;
@property(nonatomic) NSUInteger wirePort;
@property(nonatomic) CGPoint down, original, at;
@property(nonatomic,copy) void (^selectNode)(NSUInteger);
@property(nonatomic,copy) void (^edit)(NSString *,NSUInteger,NSUInteger,NSUInteger,double,NSString *);
- (void)fit;
- (NSDictionary *)kind:(NSDictionary *)node;
@end
@implementation JFXCompositionCanvas
- (instancetype)init {
    if ((self=[super initWithFrame:CGRectZero])) {
        _zoom=.8; _pan=CGPointMake(20,20); _dragging=_wiring=-1; _state=@{}; _catalog=@[];
        self.accessibilityLabel=@"Composition graph. Drag nodes and ports, drag background to pan, pinch to zoom.";
        UIPanGestureRecognizer *drag=[[UIPanGestureRecognizer alloc] initWithTarget:self action:@selector(drag:)]; drag.maximumNumberOfTouches=1; [self addGestureRecognizer:drag];
        [self addGestureRecognizer:[[UIPinchGestureRecognizer alloc] initWithTarget:self action:@selector(pinch:)]];
        [self addGestureRecognizer:[[UITapGestureRecognizer alloc] initWithTarget:self action:@selector(tap:)]];
    }
    return self;
}
- (NSDictionary *)kind:(NSDictionary *)node {
    for (NSDictionary *k in self.catalog) if ([k[@"name"] isEqual:node[@"kind"]]) return k;
    return nil;
}
- (CGPoint)world:(CGPoint)p { return CGPointMake((p.x-self.pan.x)/self.zoom,(p.y-self.pan.y)/self.zoom); }
- (NSDictionary *)hit:(CGPoint)p {
    NSArray *nodes=self.state[@"nodes"] ?: @[];
    for (NSInteger n=(NSInteger)nodes.count-1;n>=0;--n) {
        NSDictionary *node=nodes[(NSUInteger)n],*k=[self kind:node]; if (!k) continue;
        CGFloat x=[node[@"x"] doubleValue],y=[node[@"y"] doubleValue];
        for (NSUInteger side=0;side<2;++side) {
            NSArray *ports=k[side?@"outputs":@"inputs"];
            for (NSUInteger port=0;port<ports.count;++port)
                if (hypot(p.x-x-(side?210:0),p.y-y-44-port*23)<14/self.zoom) return @{@"node":@(n),@"side":@(side),@"port":@(port)};
        }
        CGFloat height=55+MAX([k[@"inputs"] count],[k[@"outputs"] count])*23;
        if (CGRectContainsPoint(CGRectMake(x,y,210,height),p)) return @{@"node":@(n)};
    }
    return nil;
}
- (void)tap:(UITapGestureRecognizer *)gesture {
    NSDictionary *hit=[self hit:[self world:[gesture locationInView:self]]];
    if (hit) { self.selected=[hit[@"node"] unsignedIntegerValue]; if (self.selectNode) self.selectNode(self.selected); [self setNeedsDisplay]; }
}
- (void)pinch:(UIPinchGestureRecognizer *)gesture {
    CGPoint screen=[gesture locationInView:self],world=[self world:screen];
    self.zoom=fmax(.1,fmin(3,self.zoom*gesture.scale)); gesture.scale=1;
    self.pan=CGPointMake(screen.x-world.x*self.zoom,screen.y-world.y*self.zoom); self.dragging=self.wiring=-1; [self setNeedsDisplay];
}
- (void)drag:(UIPanGestureRecognizer *)gesture {
    CGPoint screen=[gesture locationInView:self],p=[self world:screen]; NSArray *nodes=self.state[@"nodes"] ?: @[];
    if (gesture.state==UIGestureRecognizerStateBegan) {
        NSDictionary *hit=[self hit:p]; self.down=screen; self.dragging=self.wiring=-1;
        if (hit) {
            self.selected=[hit[@"node"] unsignedIntegerValue]; if (self.selectNode) self.selectNode(self.selected);
            if (hit[@"side"] && [hit[@"side"] unsignedIntegerValue]==1) { self.wiring=(NSInteger)self.selected; self.wirePort=[hit[@"port"] unsignedIntegerValue]; self.at=p; }
            else if (!hit[@"side"]) { self.dragging=(NSInteger)self.selected; self.original=CGPointMake([nodes[self.selected][@"x"] doubleValue],[nodes[self.selected][@"y"] doubleValue]); self.at=self.original; }
        }
    } else if (gesture.state==UIGestureRecognizerStateChanged) {
        if (self.dragging>=0) self.at=CGPointMake(fmax(-1.e6,fmin(1.e6,self.original.x+(screen.x-self.down.x)/self.zoom)),fmax(-1.e6,fmin(1.e6,self.original.y+(screen.y-self.down.y)/self.zoom)));
        else if (self.wiring>=0) self.at=p;
        else { self.pan=CGPointMake(self.pan.x+screen.x-self.down.x,self.pan.y+screen.y-self.down.y); self.down=screen; }
    } else if (gesture.state==UIGestureRecognizerStateEnded) {
        NSInteger dragging=self.dragging,wiring=self.wiring; self.dragging=self.wiring=-1;
        if (dragging>=0 && hypot(self.at.x-self.original.x,self.at.y-self.original.y)>.01 && self.edit)
            self.edit(@"node.position",(NSUInteger)dragging,0,0,self.at.x,[NSString stringWithFormat:@"%.9g",(double)self.at.y]);
        if (wiring>=0 && self.edit) {
            NSDictionary *hit=[self hit:p]; if (hit[@"side"] && [hit[@"side"] unsignedIntegerValue]==0)
                self.edit(@"node.connect",(NSUInteger)wiring,[hit[@"node"] unsignedIntegerValue],[hit[@"port"] unsignedIntegerValue],self.wirePort,@"");
        }
    } else if (gesture.state==UIGestureRecognizerStateCancelled || gesture.state==UIGestureRecognizerStateFailed) self.dragging=self.wiring=-1;
    [self setNeedsDisplay];
}
- (void)fit {
    CGFloat minx=0,miny=0,maxx=240,maxy=160;
    for (NSDictionary *n in self.state[@"nodes"]) { CGFloat x=[n[@"x"] doubleValue],y=[n[@"y"] doubleValue]; minx=fmin(minx,x); miny=fmin(miny,y); maxx=fmax(maxx,x+210); maxy=fmax(maxy,y+160); }
    self.zoom=fmax(.1,fmin(2,fmin(self.bounds.size.width/(maxx-minx+40),self.bounds.size.height/(maxy-miny+40))));
    self.pan=CGPointMake(20-minx*self.zoom,20-miny*self.zoom); [self setNeedsDisplay];
}
- (CGPoint)port:(NSUInteger)n index:(NSUInteger)p output:(BOOL)output {
    NSDictionary *node=self.state[@"nodes"][n]; CGFloat x=[node[@"x"] doubleValue],y=[node[@"y"] doubleValue];
    if ((NSInteger)n==self.dragging) { x=self.at.x; y=self.at.y; }
    return CGPointMake(x+(output?210:0),y+44+p*23);
}
- (UIColor *)color:(NSString *)type {
    return [type isEqual:@"image"]?[UIColor colorWithRed:.39 green:.76 blue:.98 alpha:1]:[type isEqual:@"color"]?[UIColor colorWithRed:.9 green:.59 blue:.35 alpha:1]:[UIColor colorWithRed:.63 green:.86 blue:.51 alpha:1];
}
- (void)line:(CGPoint)a to:(CGPoint)b color:(UIColor *)color {
    UIBezierPath *line=[UIBezierPath bezierPath]; [line moveToPoint:a]; [line addCurveToPoint:b controlPoint1:CGPointMake(a.x+60,a.y) controlPoint2:CGPointMake(b.x-60,b.y)];
    line.lineWidth=2; [color setStroke]; [line stroke];
}
- (void)drawRect:(CGRect)rect {
    [[UIColor colorWithRed:.08 green:.11 blue:.15 alpha:1] setFill]; UIRectFill(rect);
    CGContextRef context=UIGraphicsGetCurrentContext(); CGContextSaveGState(context); CGContextTranslateCTM(context,self.pan.x,self.pan.y); CGContextScaleCTM(context,self.zoom,self.zoom);
    NSArray *nodes=self.state[@"nodes"] ?: @[];
    [nodes enumerateObjectsUsingBlock:^(NSDictionary *node,NSUInteger n,BOOL *stop) {
        (void)stop; NSDictionary *kind=[self kind:node]; NSArray *inputs=node[@"inputs"];
        [inputs enumerateObjectsUsingBlock:^(id edge,NSUInteger p,BOOL *done) {
            (void)done; if (edge==NSNull.null) return;
            [self line:[self port:[edge[@"source"] unsignedIntegerValue] index:[edge[@"port"] unsignedIntegerValue] output:YES] to:[self port:n index:p output:NO] color:[self color:kind[@"inputs"][p][@"type"]]];
        }];
    }];
    [nodes enumerateObjectsUsingBlock:^(NSDictionary *node,NSUInteger n,BOOL *stop) {
        (void)stop; NSDictionary *k=[self kind:node]; if (!k) return;
        CGPoint at=[self port:n index:0 output:NO]; CGFloat x=at.x,y=at.y-44,height=55+MAX([k[@"inputs"] count],[k[@"outputs"] count])*23;
        [[UIColor colorWithRed:n==self.selected ? .16 : .21 green:n==self.selected ? .42 : .25 blue:n==self.selected ? .57 : .33 alpha:1] setFill]; UIRectFill(CGRectMake(x,y,210,height));
        if (self.state[@"output"]!=NSNull.null && [self.state[@"output"] unsignedIntegerValue]==n) { [UIColor.systemYellowColor setStroke]; UIBezierPath *border=[UIBezierPath bezierPathWithRect:CGRectMake(x,y,210,height)]; border.lineWidth=2; [border stroke]; }
        NSDictionary *style=@{NSFontAttributeName:[UIFont systemFontOfSize:12],NSForegroundColorAttributeName:UIColor.whiteColor};
        [[NSString stringWithFormat:@"%lu: %@",(unsigned long)n,node[@"label"]] drawInRect:CGRectMake(x+8,y+5,195,25) withAttributes:style];
        for (NSUInteger side=0;side<2;++side) {
            NSArray *ports=k[side?@"outputs":@"inputs"];
            [ports enumerateObjectsUsingBlock:^(NSDictionary *port,NSUInteger p,BOOL *done) {
                (void)done; CGPoint pos=[self port:n index:p output:side!=0]; [[self color:port[@"type"]] setFill]; [[UIBezierPath bezierPathWithOvalInRect:CGRectMake(pos.x-5,pos.y-5,10,10)] fill];
                [port[@"label"] drawInRect:CGRectMake(pos.x+(side?-85:10),pos.y-8,80,20) withAttributes:style];
            }];
        }
    }];
    if (self.wiring>=0 && (NSUInteger)self.wiring<nodes.count) [self line:[self port:(NSUInteger)self.wiring index:self.wirePort output:YES] to:self.at color:UIColor.systemYellowColor];
    CGContextRestoreGState(context);
}
@end

@interface JFXCompositionViewController ()
@property(nonatomic,strong) JFXMobilePlayerBridge *player;
@property(nonatomic,strong) UIStackView *content;
@property(nonatomic,strong) UIStackView *inspector;
@property(nonatomic,strong) JFXCompositionCanvas *canvas;
@property(nonatomic,strong) UIImageView *preview;
@property(nonatomic,strong) UILabel *status;
@property(nonatomic,strong) CADisplayLink *exportClock;
@property(nonatomic,copy) NSArray<NSDictionary *> *catalog;
@property(nonatomic) NSUInteger previewNode;
@property(nonatomic) BOOL previousGraphMode;
@end
@implementation JFXCompositionViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player { if ((self=[super init])) { _player=player; _previewNode=NSNotFound; _previousGraphMode=[[player graphState][@"active"] boolValue]; } return self; }
- (void)viewWillDisappear:(BOOL)animated {
    [super viewWillDisappear:animated];
    [self cancelExport];
    [self.player edit:self.previousGraphMode?@"graph":@"sequence" track:0 clip:0 target:0 value:0 text:@""];
}
- (void)cancelExport { [self.exportClock invalidate]; self.exportClock=nil; [self.player cancelVideoExport]; }
- (void)exportTick:(CADisplayLink *)link {
    (void)link; NSDictionary *progress=[self.player stepVideoExport];
    self.status.text=[progress[@"result"] intValue]==0?[NSString stringWithFormat:@"Video export: %@ frames",progress[@"frames"]]:@"Video export failed. Check resources, codecs and output path.";
    if ([progress[@"state"] intValue]!=0) { [self.exportClock invalidate]; self.exportClock=nil; }
}
- (void)dealloc { [NSNotificationCenter.defaultCenter removeObserver:self]; }
- (UIButton *)button:(NSString *)title parent:(UIStackView *)parent action:(void (^)(void))action {
    UIButton *b=[UIButton buttonWithType:UIButtonTypeSystem]; [b setTitle:title forState:UIControlStateNormal];
    [b addAction:[UIAction actionWithHandler:^(__kindof UIAction *a) { (void)a; action(); }] forControlEvents:UIControlEventTouchUpInside]; [parent addArrangedSubview:b]; return b;
}
- (UITextField *)field:(NSString *)title value:(NSString *)value parent:(UIStackView *)parent {
    UILabel *label=[[UILabel alloc] init]; label.text=title; [parent addArrangedSubview:label];
    UITextField *field=[[UITextField alloc] init]; field.text=value; field.accessibilityLabel=title; field.borderStyle=UITextBorderStyleRoundedRect; [parent addArrangedSubview:field]; return field;
}
- (double)number:(UITextField *)field { double v; NSScanner *s=[NSScanner scannerWithString:field.text ?: @""]; return [s scanDouble:&v] && s.isAtEnd?v:NAN; }
- (void)apply:(NSString *)op a:(NSUInteger)a b:(NSUInteger)b c:(NSUInteger)c value:(double)v text:(NSString *)text {
    if (![self.player edit:op track:a clip:b target:c value:v text:text]) { self.status.text=@"Edit rejected. Check node selection, port types and cycles."; return; }
    self.previewNode=NSNotFound; [self refresh];
}
- (void)viewDidLoad {
    [super viewDidLoad]; self.title=@"Node Composition"; self.catalog=[self.player nodeKinds];
    [NSNotificationCenter.defaultCenter addObserver:self selector:@selector(cancelExport) name:UIApplicationWillResignActiveNotification object:nil];
    [self.player edit:@"graph" track:0 clip:0 target:0 value:0 text:@""];
    UIScrollView *scroll=[[UIScrollView alloc] initWithFrame:self.view.bounds]; scroll.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight; [self.view addSubview:scroll];
    self.content=[[UIStackView alloc] init]; self.content.axis=UILayoutConstraintAxisVertical; self.content.spacing=8; self.content.translatesAutoresizingMaskIntoConstraints=NO; [scroll addSubview:self.content];
    [NSLayoutConstraint activateConstraints:@[[self.content.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],[self.content.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],[self.content.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],[self.content.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],[self.content.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]]];
    __weak JFXCompositionViewController *weak=self;
    [self button:@"Done" parent:self.content action:^{ [weak dismissViewControllerAnimated:YES completion:nil]; }];
    self.preview=[[UIImageView alloc] init]; self.preview.contentMode=UIViewContentModeScaleAspectFit; [self.preview.heightAnchor constraintEqualToConstant:180].active=YES; [self.content addArrangedSubview:self.preview];
    self.status=[[UILabel alloc] init]; self.status.numberOfLines=0; [self.content addArrangedSubview:self.status];
    UITextField *seconds=[self field:@"Preview seconds" value:@"0" parent:self.content];
    [self button:@"Seek composition time" parent:self.content action:^{ weak.seconds=[weak number:seconds]; [weak showPreview]; }];
    self.canvas=[[JFXCompositionCanvas alloc] init]; self.canvas.catalog=self.catalog; [self.canvas.heightAnchor constraintEqualToConstant:320].active=YES; [self.content addArrangedSubview:self.canvas];
    for (UIGestureRecognizer *gesture in self.canvas.gestureRecognizers) if ([gesture isKindOfClass:UIPanGestureRecognizer.class]) [scroll.panGestureRecognizer requireGestureRecognizerToFail:gesture];
    self.canvas.selectNode=^(NSUInteger n) { weak.selectedNode=n; if (weak.previewNode!=NSNotFound) weak.previewNode=n; [weak refresh]; };
    self.canvas.edit=^(NSString *op,NSUInteger a,NSUInteger b,NSUInteger c,double v,NSString *text) { [weak apply:op a:a b:b c:c value:v text:text]; };
    [self button:@"Fit nodes" parent:self.content action:^{ [weak.canvas fit]; }];
    UIButton *add=[self button:@"Add node" parent:self.content action:^{}]; NSMutableArray *actions=[NSMutableArray array];
    for (NSDictionary *kind in self.catalog) [actions addObject:[UIAction actionWithTitle:[NSString stringWithFormat:@"%@ / %@",kind[@"category"],kind[@"label"]] image:nil identifier:nil handler:^(__kindof UIAction *action) {
        (void)action; NSUInteger n=[[weak.player graphState][@"nodes"] count];
        [weak apply:@"node.add" a:0 b:0 c:0 value:0 text:kind[@"name"]]; if ([[weak.player graphState][@"nodes"] count]>n) { weak.selectedNode=n; [weak refresh]; }
    }]];
    add.menu=[UIMenu menuWithTitle:@"Typed node library" children:actions]; add.showsMenuAsPrimaryAction=YES;
    for (NSString *op in @[@"undo",@"redo",@"node.duplicate",@"node.reset",@"node.remove"]) [self button:op parent:self.content action:^{ [weak apply:op a:weak.selectedNode b:0 c:0 value:0 text:@""]; }];
    [self button:@"Set output" parent:self.content action:^{ [weak apply:@"node.output" a:weak.selectedNode b:0 c:0 value:0 text:@""]; }];
    [self button:@"Preview selected node" parent:self.content action:^{ weak.previewNode=weak.selectedNode; [weak showPreview]; }];
    [self button:@"Preview composition output" parent:self.content action:^{ weak.previewNode=NSNotFound; [weak showPreview]; }];
    self.inspector=[[UIStackView alloc] init]; self.inspector.axis=UILayoutConstraintAxisVertical; self.inspector.spacing=6; [self.content addArrangedSubview:self.inspector];
    NSDictionary *state=[self.player graphState]; UITextField *width=[self field:@"Composition width" value:[state[@"width"] stringValue] ?: @"320" parent:self.content];
    UITextField *height=[self field:@"Composition height" value:[state[@"height"] stringValue] ?: @"180" parent:self.content];
    for (NSString *op in @[@"graph.size",@"graph.new"]) [self button:op parent:self.content action:^{
        double w=[weak number:width],h=[weak number:height];
        if (!isfinite(w) || !isfinite(h) || w<1 || w>4096 || h<1 || h>4096 || floor(w)!=w || floor(h)!=h) { weak.status.text=@"Invalid raster dimensions."; return; }
        [weak apply:op a:(NSUInteger)w b:(NSUInteger)h c:0 value:0 text:@""];
    }];
    UITextField *file=[self field:@"Project file path" value:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/composition.jfx"] parent:self.content];
    [self button:@"Open composition" parent:self.content action:^{
        NSString *doc=[NSString stringWithContentsOfFile:file.text encoding:NSUTF8StringEncoding error:nil];
        if (doc && [weak.player loadComposition:doc]) { weak.previewNode=NSNotFound; [weak refresh]; }
        else weak.status.text=@"Unable to open composition.";
    }];
    [self button:@"Save composition" parent:self.content action:^{ if (![[weak.player saveDocument] writeToFile:file.text atomically:YES encoding:NSUTF8StringEncoding error:nil]) weak.status.text=@"Unable to save composition."; }];
    UITextField *output=[self field:@"Export path (PPM)" value:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/composition.ppm"] parent:self.content];
    [self button:@"Export composition" parent:self.content action:^{ if (![weak.player writeCompositionAtSeconds:weak.seconds path:output.text ?: @""]) weak.status.text=@"Unable to export composition."; }];
    UITextField *video=[self field:@"Encoded video path (.mp4/.mov/.mkv)" value:[NSHomeDirectory() stringByAppendingPathComponent:@"Documents/composition.mp4"] parent:self.content];
    UITextField *first=[self field:@"Video start frame (30 fps)" value:@"0" parent:self.content];
    UITextField *count=[self field:@"Video frame count" value:@"300" parent:self.content];
    [self button:@"Export composition video" parent:self.content action:^{
        double start=[weak number:first],frames=[weak number:count];
        if (!isfinite(start) || !isfinite(frames) || start<0 || frames<1 || start>1.e7 || frames>1.e7 || floor(start)!=start || floor(frames)!=frames ||
            ![weak.player beginVideoExport:video.text ?: @"" startFrame:(NSUInteger)start frameCount:(NSUInteger)frames audio:NO]) {
            weak.status.text=@"Unable to begin video export. Check frame range, codec and output path."; return;
        }
        weak.exportClock=[CADisplayLink displayLinkWithTarget:weak selector:@selector(exportTick:)];
        [weak.exportClock addToRunLoop:NSRunLoop.mainRunLoop forMode:NSRunLoopCommonModes];
    }];
    [self button:@"Cancel video export" parent:self.content action:^{ [weak cancelExport]; }];
    [self refresh];
}
- (void)showPreview {
    NSData *pixels=[self.player previewGraphNode:self.previewNode seconds:self.seconds];
    if (!pixels) { self.status.text=@"Unable to render composition. Check resource paths."; return; }
    CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels); CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
    CGImageRef image=CGImageCreate(320,180,8,32,1280,space,kCGBitmapByteOrderDefault|kCGImageAlphaLast,provider,NULL,NO,kCGRenderingIntentDefault);
    self.preview.image=[UIImage imageWithCGImage:image]; CGImageRelease(image); CGColorSpaceRelease(space); CGDataProviderRelease(provider);
}
- (void)refresh {
    if (!self.isViewLoaded) return; NSDictionary *state=[self.player graphState]; NSArray *nodes=state[@"nodes"] ?: @[];
    if (self.selectedNode>=nodes.count) self.selectedNode=0;
    if (self.previewNode!=NSNotFound && self.previewNode>=nodes.count) self.previewNode=NSNotFound;
    self.canvas.state=state; self.canvas.selected=self.selectedNode; [self.canvas setNeedsDisplay]; self.status.text=@"";
    for (UIView *v in self.inspector.arrangedSubviews) { [self.inspector removeArrangedSubview:v]; [v removeFromSuperview]; }
    __weak JFXCompositionViewController *weak=self; NSUInteger n=self.selectedNode;
    if (n<nodes.count) {
        NSDictionary *node=nodes[n],*k=[self.canvas kind:node];
        UITextField *label=[self field:@"Node label" value:node[@"label"] parent:self.inspector];
        [self button:@"Rename node" parent:self.inspector action:^{ [weak apply:@"node.label" a:n b:0 c:0 value:0 text:label.text ?: @""]; }];
        [k[@"inputs"] enumerateObjectsUsingBlock:^(NSDictionary *port,NSUInteger p,BOOL *stop) {
            (void)stop; id edge=node[@"inputs"][p]; NSString *source=edge==NSNull.null?@"Disconnected":[NSString stringWithFormat:@"Node %@ / port %@",edge[@"source"],edge[@"port"]];
            UIButton *input=[weak button:[NSString stringWithFormat:@"%@ (%@): %@",port[@"label"],port[@"type"],source] parent:weak.inspector action:^{}];
            NSMutableArray *choices=[NSMutableArray arrayWithObject:[UIAction actionWithTitle:@"Disconnect" image:nil identifier:nil handler:^(__kindof UIAction *a) { (void)a; [weak apply:@"node.disconnect" a:n b:p c:0 value:0 text:@""]; }]];
            [nodes enumerateObjectsUsingBlock:^(NSDictionary *other,NSUInteger s,BOOL *done) {
                (void)done; if (s==n) return; NSDictionary *sk=[weak.canvas kind:other];
                [sk[@"outputs"] enumerateObjectsUsingBlock:^(NSDictionary *output,NSUInteger o,BOOL *finished) {
                    (void)finished; if (![port[@"type"] isEqual:output[@"type"]]) return;
                    [choices addObject:[UIAction actionWithTitle:[NSString stringWithFormat:@"%lu: %@ / %@",(unsigned long)s,other[@"label"],output[@"label"]] image:nil identifier:nil handler:^(__kindof UIAction *a) { (void)a; [weak apply:@"node.connect" a:s b:n c:p value:o text:@""]; }]];
                }];
            }];
            input.menu=[UIMenu menuWithTitle:port[@"label"] children:choices]; input.showsMenuAsPrimaryAction=YES;
        }];
        [k[@"params"] enumerateObjectsUsingBlock:^(NSDictionary *param,NSUInteger p,BOOL *stop) {
            (void)stop; UITextField *value=[weak field:[NSString stringWithFormat:@"%@ [%@, %@]%@",param[@"label"],param[@"min"],param[@"max"],[param[@"integer"] boolValue]?@" integer":@""] value:[node[@"values"][p] stringValue] parent:weak.inspector];
            [weak button:[@"Apply " stringByAppendingString:param[@"label"]] parent:weak.inspector action:^{ [weak apply:@"node.param" a:n b:0 c:0 value:[weak number:value] text:param[@"name"]]; }];
        }];
        [k[@"strings"] enumerateObjectsUsingBlock:^(NSString *name,NSUInteger s,BOOL *stop) {
            (void)stop; UITextField *path=[weak field:name value:node[@"strings"][s] parent:weak.inspector];
            [weak button:@"Apply resource path" parent:weak.inspector action:^{ [weak apply:@"node.path" a:n b:s c:0 value:0 text:path.text ?: @""]; }];
            [weak button:@"Clear resource path" parent:weak.inspector action:^{ [weak apply:@"node.path" a:n b:s c:0 value:0 text:@""]; }];
        }];
    }
    [self showPreview];
}
@end
