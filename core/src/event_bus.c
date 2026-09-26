#include "tilly/allocator.h"
#include "tilly/logger.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>

#define MAX_EVENT_TYPES 64
#define MAX_SUBSCRIBERS 128

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

typedef struct {
    void (*handler)(jfx_event_type_t type, void *data, void *userdata);
    void *userdata;
} subscriber_t;

typedef struct {
    subscriber_t subscribers[MAX_SUBSCRIBERS];
    uint32_t count;
    pthread_mutex_t lock;
} event_type_t;

static event_type_t g_event_types[MAX_EVENT_TYPES];
static bool g_event_bus_initialized = false;

void event_bus_init(void) {
    if (g_event_bus_initialized) return;
    
    for (uint32_t i = 0; i < MAX_EVENT_TYPES; i++) {
        pthread_mutex_init(&g_event_types[i].lock, NULL);
    }
    
    g_event_bus_initialized = true;
    tilly_log_debug("event_bus", "Event bus initialized");
}

void event_bus_shutdown(void) {
    if (!g_event_bus_initialized) return;
    
    for (uint32_t i = 0; i < MAX_EVENT_TYPES; i++) {
        pthread_mutex_destroy(&g_event_types[i].lock);
    }
    
    g_event_bus_initialized = false;
    tilly_log_debug("event_bus", "Event bus shutdown");
}

bool event_subscribe(jfx_event_type_t type, void (*handler)(jfx_event_type_t, void *, void *), void *userdata) {
    if (!g_event_bus_initialized || type >= MAX_EVENT_TYPES || !handler) return false;
    
    event_type_t *et = &g_event_types[type];
    pthread_mutex_lock(&et->lock);
    
    if (et->count >= MAX_SUBSCRIBERS) {
        pthread_mutex_unlock(&et->lock);
        return false;
    }
    
    et->subscribers[et->count++] = (subscriber_t){handler, userdata};
    
    pthread_mutex_unlock(&et->lock);
    return true;
}

bool event_unsubscribe(jfx_event_type_t type, void (*handler)(jfx_event_type_t, void *, void *)) {
    if (!g_event_bus_initialized || type >= MAX_EVENT_TYPES || !handler) return false;
    
    event_type_t *et = &g_event_types[type];
    pthread_mutex_lock(&et->lock);
    
    for (uint32_t i = 0; i < et->count; i++) {
        if (et->subscribers[i].handler == handler) {
            et->subscribers[i] = et->subscribers[--et->count];
            pthread_mutex_unlock(&et->lock);
            return true;
        }
    }
    
    pthread_mutex_unlock(&et->lock);
    return false;
}

void event_publish(jfx_event_type_t type, void *data) {
    if (!g_event_bus_initialized || type >= MAX_EVENT_TYPES) return;
    
    event_type_t *et = &g_event_types[type];
    pthread_mutex_lock(&et->lock);
    
    for (uint32_t i = 0; i < et->count; i++) {
        subscriber_t sub = et->subscribers[i];
        if (sub.handler) {
            sub.handler(type, data, sub.userdata);
        }
    }
    
    pthread_mutex_unlock(&et->lock);
}

uint32_t event_subscriber_count(jfx_event_type_t type) {
    if (!g_event_bus_initialized || type >= MAX_EVENT_TYPES) return 0;
    return g_event_types[type].count;
}