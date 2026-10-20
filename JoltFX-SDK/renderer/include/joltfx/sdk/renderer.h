#ifndef JOLTFX_SDK_RENDERER_H
#define JOLTFX_SDK_RENDERER_H
#include <jfx/jfx_engine.h>
#ifdef __cplusplus
extern "C" {
#endif

#define JFX_RENDERER_ABI_MAJOR 1u
#define JFX_RENDERER_ABI_MINOR 0u
#if defined(_WIN32)
#define JFX_RENDERER_EXPORT __declspec(dllexport)
#elif defined(__GNUC__) || defined(__clang__)
#define JFX_RENDERER_EXPORT __attribute__((visibility("default")))
#else
#define JFX_RENDERER_EXPORT
#endif

typedef enum { JFX_RENDERER_DEVICE_CPU=0, JFX_RENDERER_DEVICE_GPU=1 } jfx_renderer_device_t;
typedef enum { JFX_RENDERER_BACKEND_INGAGI=0, JFX_RENDERER_BACKEND_VULKAN=1 } jfx_renderer_backend_t;
/* Implementation support, not hardware/host availability. Query host.available
 * for the backing service; a driver can still fail during rendering. */
enum {
    JFX_RENDERER_CAP_CPU=1u<<0,
    JFX_RENDERER_CAP_GPU=1u<<1,
    JFX_RENDERER_CAP_SIMD=1u<<2
};
/* Borrowed immutable scene snapshot. Synchronous callbacks may retain no scene,
 * output or frame pointers. No implicit device fallback: errors leave output
 * untouched. All dimensions are 1..2048; output is tightly packed straight RGBA8.
 * Exposure is display-referred RGB gain in stops (-16..16), alpha unchanged. */
typedef struct {
    size_t size;
    const void *scene;
    double seconds;
    uint32_t width,height;
    jfx_renderer_device_t device;
    uint32_t samples,bounces,timeout_ms;
    float reflectivity,exposure;
} jfx_renderer_frame_t;
/* Host owns context and allocation lifetime. Allocations must be paired with
 * release from this SAME table. Backend services must not dispatch back into
 * the renderer module. Availability is compiled support, not a driver probe.
 * Ingagi is the native MonkeyTrace ECS tracer; Vulkan is GPU rasterization.
 * Scene preparation samples keys, cloners and drivers at frame.seconds.
 * No callback or exported entry point may throw exceptions across this ABI. */
typedef struct {
    size_t size;
    uint32_t abi_major,abi_minor;
    void *context;
    void *(*allocate)(void *context,size_t bytes,size_t alignment);
    void (*release)(void *context,void *pointer);
    bool (*available)(void *context,jfx_renderer_backend_t backend);
    jfx_result_t (*render_scene)(void *context,const jfx_renderer_frame_t *frame,
        jfx_renderer_backend_t backend,uint8_t *out_rgba,size_t capacity);
} jfx_renderer_host_t;
typedef jfx_result_t (*jfx_renderer_render_fn)(const jfx_renderer_host_t *host,
    const jfx_renderer_frame_t *frame,uint8_t *out_rgba,size_t capacity);
typedef struct {
    size_t size;
    uint32_t abi_major,minimum_abi_minor;
    const char *identifier,*name; /* Static UTF-8, valid until module close. */
    uint32_t version,capabilities;
    jfx_renderer_render_fn render;
} jfx_renderer_definition_t;
typedef jfx_result_t (*jfx_renderer_entry_fn)(uint32_t major,uint32_t minor,
    jfx_renderer_definition_t *out_definition);
/* Native renderer modules export this symbol; no engine linkage is required. */
JFX_RENDERER_EXPORT jfx_result_t jfx_renderer_entry(uint32_t major,uint32_t minor,
    jfx_renderer_definition_t *out_definition);
#ifdef __cplusplus
}
#endif
#endif
