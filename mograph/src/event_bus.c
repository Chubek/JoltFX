#include "jfx/jfx_events.h"
#include <pthread.h>
#include <string.h>
#include "tilly/containers.h"

#define MAX_SUBSCRIBERS 128
typedef struct { jfx_event_handler_t handler; void *userdata; } subscriber_t;
typedef kvec_t(subscriber_t) event_type_t;
static event_type_t events[JFX_EVENT_COUNT];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static bool initialized;

bool event_bus_init(void) {
    pthread_mutex_lock(&lock);
    bool fresh = !initialized;
    if (fresh) { memset(events, 0, sizeof(events)); initialized = true; }
    pthread_mutex_unlock(&lock);
    return fresh;
}
void event_bus_shutdown(void) {
    pthread_mutex_lock(&lock);
    initialized = false;
    for (unsigned i = 0; i < JFX_EVENT_COUNT; ++i) tilly_vec_destroy(events[i]);
    pthread_mutex_unlock(&lock);
}
static bool valid(jfx_event_type_t type) { return type >= 0 && type < JFX_EVENT_COUNT; }
bool event_subscribe(jfx_event_type_t type, jfx_event_handler_t handler, void *userdata) {
    if (!valid(type) || !handler) return false;
    pthread_mutex_lock(&lock);
    event_type_t *e = &events[type];
    bool ok = initialized && e->n < MAX_SUBSCRIBERS && tilly_vec_reserve(e, e->n + 1);
    if (ok) e->a[e->n++] = (subscriber_t){handler, userdata};
    pthread_mutex_unlock(&lock);
    return ok;
}
bool event_unsubscribe(jfx_event_type_t type, jfx_event_handler_t handler) {
    if (!valid(type) || !handler) return false;
    pthread_mutex_lock(&lock);
    event_type_t *e = &events[type];
    bool found = false;
    if (initialized) for (unsigned i = 0; i < e->n; ++i) {
        if (e->a[i].handler == handler) {
            for (unsigned j = i + 1; j < e->n; ++j) e->a[j - 1] = e->a[j];
            --e->n; found = true; break;
        }
    }
    pthread_mutex_unlock(&lock);
    return found;
}
bool event_unsubscribe_user(jfx_event_type_t type,jfx_event_handler_t handler,void *userdata) {
    if (!valid(type) || !handler) return false;
    pthread_mutex_lock(&lock);
    event_type_t *e=&events[type]; bool found=false;
    if (initialized) for (size_t i=0;i<e->n;++i) {
        if (e->a[i].handler==handler && e->a[i].userdata==userdata) {
            memmove(e->a+i,e->a+i+1,(e->n-i-1)*sizeof(*e->a)); --e->n; found=true; break;
        }
    }
    pthread_mutex_unlock(&lock); return found;
}
void event_publish(jfx_event_type_t type, void *data) {
    if (!valid(type)) return;
    subscriber_t snapshot[MAX_SUBSCRIBERS];
    pthread_mutex_lock(&lock);
    size_t count = initialized ? events[type].n : 0;
    if (count) memcpy(snapshot, events[type].a, count * sizeof(*snapshot));
    pthread_mutex_unlock(&lock);
    for (unsigned i = 0; i < count; ++i) snapshot[i].handler(type, data, snapshot[i].userdata);
}
uint32_t event_subscriber_count(jfx_event_type_t type) {
    if (!valid(type)) return 0;
    pthread_mutex_lock(&lock);
    uint32_t count = initialized ? (uint32_t)events[type].n : 0;
    pthread_mutex_unlock(&lock);
    return count;
}
