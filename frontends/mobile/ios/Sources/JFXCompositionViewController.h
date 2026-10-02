#import <UIKit/UIKit.h>
#import "JFXMobilePlayerBridge.h"

/* Embed in a navigation controller or present from the NLE. Requires iOS 14+.
 * Node layout, values, edges and history belong to the shared editor. */
@interface JFXCompositionViewController : UIViewController
- (instancetype)initWithPlayer:(JFXMobilePlayerBridge *)player;
- (void)refresh;
@property(nonatomic) NSUInteger selectedNode;
@property(nonatomic) NSTimeInterval seconds;
@end
