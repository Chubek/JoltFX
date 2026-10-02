#import <UIKit/UIKit.h>
#import "JFXMobilePlayerBridge.h"

/* Embed alongside the timeline. Both sections edit the selected clip's real
 * stack; the preview is rendered by the shared editor. Requires iOS 14+. */
@interface JFXColorViewController : UIViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player;
- (void)refresh;
@property(nonatomic) NSUInteger track;
@property(nonatomic) NSUInteger clip;
@end
