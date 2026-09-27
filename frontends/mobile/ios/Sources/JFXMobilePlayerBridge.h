#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface JFXMobilePlayerBridge : NSObject
- (nullable instancetype)initWithWidth:(NSUInteger)width
                                height:(NSUInteger)height
                              duration:(NSTimeInterval)duration;
- (BOOL)tap;
- (BOOL)swipeByPixels:(CGFloat)pixels;
- (BOOL)pinchByFactor:(CGFloat)factor;
- (BOOL)renderElapsed:(NSTimeInterval)elapsed;
@end

NS_ASSUME_NONNULL_END
