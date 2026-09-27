#import "JFXMobilePlayerBridge.h"

#include "jfx/mobile_player.h"

@interface JFXMobilePlayerBridge () {
    jfx_mobile_player_t *_player;
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

- (void)dealloc { jfx_mobile_player_destroy(_player); }
- (BOOL)tap { return jfx_mobile_player_tap(_player) == JFX_SUCCESS; }
- (BOOL)swipeByPixels:(CGFloat)pixels { return jfx_mobile_player_swipe(_player, pixels) == JFX_SUCCESS; }
- (BOOL)pinchByFactor:(CGFloat)factor { return jfx_mobile_player_pinch(_player, factor) == JFX_SUCCESS; }
- (BOOL)renderElapsed:(NSTimeInterval)elapsed { return jfx_mobile_player_render(_player, elapsed) == JFX_SUCCESS; }

@end
