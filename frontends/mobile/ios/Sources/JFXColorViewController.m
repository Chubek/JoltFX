#import "JFXColorViewController.h"

@interface JFXColorViewController ()
@property(nonatomic,strong) JFXMobilePlayerBridge *player;
@property(nonatomic,strong) UIStackView *content;
@property(nonatomic,strong) UIImageView *preview;
@property(nonatomic,strong) UILabel *status;
@end
@implementation JFXColorViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player {
    if ((self=[super init])) _player=player;
    return self;
}
- (void)viewDidLoad {
    [super viewDidLoad];
    UIScrollView *scroll=[[UIScrollView alloc] initWithFrame:self.view.bounds];
    scroll.autoresizingMask=UIViewAutoresizingFlexibleWidth|UIViewAutoresizingFlexibleHeight;
    [self.view addSubview:scroll];
    self.content=[[UIStackView alloc] init]; self.content.axis=UILayoutConstraintAxisVertical;
    self.content.spacing=10; self.content.translatesAutoresizingMaskIntoConstraints=NO;
    [scroll addSubview:self.content];
    [NSLayoutConstraint activateConstraints:@[
        [self.content.leadingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.leadingAnchor constant:16],
        [self.content.trailingAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.trailingAnchor constant:-16],
        [self.content.topAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.topAnchor constant:16],
        [self.content.bottomAnchor constraintEqualToAnchor:scroll.contentLayoutGuide.bottomAnchor constant:-16],
        [self.content.widthAnchor constraintEqualToAnchor:scroll.frameLayoutGuide.widthAnchor constant:-32]]];
    [self refresh];
}
- (UIButton *)button:(NSString *)title action:(void (^)(void))action {
    UIButton *button=[UIButton buttonWithType:UIButtonTypeSystem];
    [button setTitle:title forState:UIControlStateNormal];
    [button addAction:[UIAction actionWithHandler:^(__kindof UIAction *sender) { (void)sender; action(); }] forControlEvents:UIControlEventTouchUpInside];
    return button;
}
- (void)apply:(NSString *)op layer:(NSUInteger)layer value:(double)value text:(NSString *)text {
    if ([self.player editColor:op track:self.track clip:self.clip layer:layer value:value text:text]) [self refresh];
    else self.status.text=@"Color edit rejected. Check the value or LUT file.";
}
- (void)refresh {
    for (UIView *view in self.content.arrangedSubviews) { [self.content removeArrangedSubview:view]; [view removeFromSuperview]; }
    self.preview=[[UIImageView alloc] init]; self.preview.contentMode=UIViewContentModeScaleAspectFit;
    [self.preview.heightAnchor constraintEqualToConstant:180].active=YES; [self.content addArrangedSubview:self.preview];
    self.status=[[UILabel alloc] init]; self.status.numberOfLines=0; [self.content addArrangedSubview:self.status];
    NSArray *catalog=[self.player colorOperators];
    __weak JFXColorViewController *weakSelf=self;
    for (NSString *section in @[@"calibration",@"grade"]) {
        UIStackView *panel=[[UIStackView alloc] init]; panel.axis=UILayoutConstraintAxisVertical; panel.spacing=8;
        panel.accessibilityLabel=[section isEqualToString:@"grade"]?@"Color Grading":@"Color Calibration";
        UILabel *heading=[[UILabel alloc] init]; heading.text=panel.accessibilityLabel; heading.font=[UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
        [panel addArrangedSubview:heading]; [self.content addArrangedSubview:panel];
        NSMutableArray *items=[NSMutableArray array];
        for (NSDictionary *op in catalog) if ([op[@"section"] isEqual:section])
            [items addObject:[UIAction actionWithTitle:op[@"label"] image:nil identifier:nil handler:^(__kindof UIAction *action) {
                (void)action; [weakSelf apply:[section stringByAppendingString:@".add"] layer:0 value:0 text:op[@"name"]];
            }]];
        UIButton *add=[UIButton buttonWithType:UIButtonTypeSystem]; [add setTitle:@"Add color operator" forState:UIControlStateNormal];
        add.menu=[UIMenu menuWithTitle:@"Operators" children:items]; add.showsMenuAsPrimaryAction=YES; [panel addArrangedSubview:add];
        NSArray *layers=[self.player colorLayers:section track:self.track clip:self.clip];
        [layers enumerateObjectsUsingBlock:^(NSDictionary *layer,NSUInteger index,BOOL *stop) {
            (void)stop;
            NSDictionary *op=nil;
            for (NSDictionary *candidate in catalog) if ([candidate[@"name"] isEqual:layer[@"name"]]) { op=candidate; break; }
            if (!op) return;
            UILabel *label=[[UILabel alloc] init]; label.text=op[@"label"]; [panel addArrangedSubview:label];
            NSArray *params=op[@"params"], *values=layer[@"values"];
            for (NSUInteger p=0;p<params.count;++p) {
                NSDictionary *param=params[p];
                UITextField *field=[[UITextField alloc] init]; field.borderStyle=UITextBorderStyleRoundedRect;
                field.text=[values[p] stringValue]; field.accessibilityLabel=param[@"label"];
                UILabel *caption=[[UILabel alloc] init]; caption.text=[NSString stringWithFormat:@"%@ [%@, %@]",param[@"label"],param[@"min"],param[@"max"]];
                [panel addArrangedSubview:caption]; [panel addArrangedSubview:field];
                [panel addArrangedSubview:[weakSelf button:@"Apply value" action:^{
                    NSScanner *scanner=[NSScanner scannerWithString:field.text ?: @""]; double v;
                    if ([scanner scanDouble:&v] && scanner.isAtEnd) [weakSelf apply:[section stringByAppendingString:@".param"] layer:index value:v text:param[@"name"]];
                    else weakSelf.status.text=@"Enter a numeric value.";
                }]];
            }
            if ([op[@"path"] boolValue]) {
                UITextField *path=[[UITextField alloc] init]; path.text=layer[@"path"]; path.placeholder=@"LUT file path";
                [panel addArrangedSubview:path];
                [panel addArrangedSubview:[weakSelf button:@"Load / clear LUT" action:^{ [weakSelf apply:[section stringByAppendingString:@".path"] layer:index value:0 text:path.text ?: @""]; }]];
            }
            [panel addArrangedSubview:[weakSelf button:[layer[@"enabled"] boolValue]?@"Bypass":@"Enable" action:^{
                [weakSelf apply:[section stringByAppendingString:@".enabled"] layer:index value:![layer[@"enabled"] boolValue] text:@""];
            }]];
            for (NSString *action in @[@"reset",@"remove"]) [panel addArrangedSubview:[weakSelf button:action action:^{
                [weakSelf apply:[section stringByAppendingFormat:@".%@",action] layer:index value:0 text:@""];
            }]];
            if (index>0) [panel addArrangedSubview:[weakSelf button:@"Move up" action:^{
                [weakSelf apply:[section stringByAppendingString:@".move"] layer:index value:(double)index-1 text:@""];
            }]];
            if (index+1<layers.count) [panel addArrangedSubview:[weakSelf button:@"Move down" action:^{
                [weakSelf apply:[section stringByAppendingString:@".move"] layer:index value:(double)index+1 text:@""];
            }]];
        }];
    }
    NSData *pixels=[self.player previewRGBA];
    if (pixels) {
        CGDataProviderRef provider=CGDataProviderCreateWithCFData((__bridge CFDataRef)pixels);
        CGColorSpaceRef space=CGColorSpaceCreateDeviceRGB();
        CGImageRef image=CGImageCreate(320,180,8,32,320*4,space,kCGBitmapByteOrderDefault|kCGImageAlphaLast,provider,NULL,NO,kCGRenderingIntentDefault);
        self.preview.image=[UIImage imageWithCGImage:image];
        CGImageRelease(image); CGColorSpaceRelease(space); CGDataProviderRelease(provider);
    } else self.status.text=@"Unable to render color preview.";
}
@end
