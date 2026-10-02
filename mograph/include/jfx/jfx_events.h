#ifndef JFX_EVENTS_H
#define JFX_EVENTS_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    JFX_EVENT_FRAME_BEGIN = 0,
    JFX_EVENT_FRAME_END,
    JFX_EVENT_KERNEL_SUBMIT,
    JFX_EVENT_KERNEL_COMPLETE,
    JFX_EVENT_KERNEL_ERROR,
    JFX_EVENT_RESOURCE_ALLOC,
    JFX_EVENT_RESOURCE_FREE,
    JFX_EVENT_TIMELINE_PLAY,
    JFX_EVENT_TIMELINE_PAUSE,
    JFX_EVENT_ASSET_LOAD,
    JFX_EVENT_ASSET_UNLOAD,
    JFX_EVENT_UI_INPUT,
    JFX_EVENT_PLUGIN_LOAD,
    JFX_EVENT_PLUGIN_UNLOAD,
    JFX_EVENT_COUNT
} jfx_event_type_t;

typedef void (*jfx_event_handler_t)(jfx_event_type_t type, void *data, void *userdata);

// Initialize event bus
bool event_bus_init(void);

// Shutdown event bus
void event_bus_shutdown(void);

// Subscribe to an event type
bool event_subscribe(jfx_event_type_t type, jfx_event_handler_t handler, void *userdata);

// Unsubscribe from an event type
bool event_unsubscribe(jfx_event_type_t type, jfx_event_handler_t handler);

// Invoke a snapshot of subscribers synchronously, without holding the registry lock
void event_publish(jfx_event_type_t type, void *data);

// Get subscriber count for an event type
uint32_t event_subscriber_count(jfx_event_type_t type);

#ifdef __cplusplus
}
#endif

#endif // JFX_EVENTS_H
