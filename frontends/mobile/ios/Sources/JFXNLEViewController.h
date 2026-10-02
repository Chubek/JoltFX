#import <UIKit/UIKit.h>
#import "JFXMobilePlayerBridge.h"

NS_ASSUME_NONNULL_BEGIN
/** Embeddable NLE. All edits, undo/redo, media and color processing share Core. */
@interface JFXNLEViewController : UIViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player;
@property(nonatomic) NSUInteger track;
@property(nonatomic) NSUInteger clip;
@property(nonatomic) NSUInteger frame;
- (void)refresh;
@end
NS_ASSUME_NONNULL_END
