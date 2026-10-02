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
/* The same independent sections and kernel descriptors as desktop/web. */
- (NSArray<NSDictionary *> *)colorOperators;
- (NSArray<NSDictionary *> *)colorLayers:(NSString *)section track:(NSUInteger)track clip:(NSUInteger)clip;
- (BOOL)editColor:(NSString *)operation track:(NSUInteger)track clip:(NSUInteger)clip
           layer:(NSUInteger)layer value:(double)value text:(NSString *)text;
- (nullable NSData *)previewRGBA;
- (NSDictionary *)sequenceState;
- (BOOL)edit:(NSString *)operation track:(NSUInteger)track clip:(NSUInteger)clip
      target:(NSUInteger)target value:(double)value text:(NSString *)text;
- (BOOL)seekSeconds:(NSTimeInterval)seconds;
- (BOOL)loadDocument:(NSString *)document;
- (nullable NSString *)saveDocument;
- (BOOL)writeFrame:(NSUInteger)frame path:(NSString *)path;
/* Shared stereo mixer streamed to AVAudioEngine. Pump from the display clock. */
- (BOOL)pumpAudioAtSeconds:(NSTimeInterval)seconds;
- (void)stopAudio;
- (BOOL)beginVideoExport:(NSString *)path startFrame:(NSUInteger)start
             frameCount:(NSUInteger)count audio:(BOOL)audio;
/* result: engine status; state: 0 running, 1 complete, 2 cancelled, 3 failed. */
- (NSDictionary *)stepVideoExport;
- (void)cancelVideoExport;
- (NSArray<NSDictionary *> *)nodeKinds;
- (NSDictionary *)graphState;
- (BOOL)loadComposition:(NSString *)document;
/* NSNotFound previews the composition output, without changing its selection. */
- (nullable NSData *)previewGraphNode:(NSUInteger)node seconds:(NSTimeInterval)seconds;
- (BOOL)writeCompositionAtSeconds:(NSTimeInterval)seconds path:(NSString *)path;
@end

NS_ASSUME_NONNULL_END
