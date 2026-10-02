#import "JFXAppDelegate.h"
#import "JFXMobilePlayerBridge.h"
#import "JFXNLEViewController.h"

@implementation JFXAppDelegate
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    (void)application; (void)options;
    self.window = [[UIWindow alloc] initWithFrame:UIScreen.mainScreen.bounds];
    JFXMobilePlayerBridge *player = [[JFXMobilePlayerBridge alloc] initWithWidth:320 height:180 duration:60];
    if (player) {
        self.window.rootViewController = [[JFXNLEViewController alloc] initWithPlayer:player];
    } else {
        UIViewController *error = [[UIViewController alloc] init];
        error.view.backgroundColor = UIColor.systemBackgroundColor;
        UILabel *label = [[UILabel alloc] initWithFrame:error.view.bounds];
        label.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
        label.text = @"Unable to initialize JoltFX.";
        label.textAlignment = NSTextAlignmentCenter;
        [error.view addSubview:label];
        self.window.rootViewController = error;
    }
    [self.window makeKeyAndVisible];
    return YES;
}
@end
