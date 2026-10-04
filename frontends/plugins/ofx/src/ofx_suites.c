/* OfxParameterSuiteV1 and OfxImageEffectSuiteV1 for the OFX host adapter.
 *
 * A plugin declares its controls with paramDefine() during describe-in-context,
 * then reads and writes them through the parameter suite while rendering.
 * Values live here; the engine's float RGBA buffers are what the image suite
 * hands back.
 *
 * Handle discipline: OFX passes only the parameter or clip handle to most suite
 * calls, so both carry an owner back-pointer. Several suite entry points are
 * declared variadic in the OFX 1.5 headers (paramGetValue and friends take
 * their value pointer through `...`), so those implementations must be
 * variadic too and pull their argument out with va_arg. Getting that wrong is a
 * compile error against the vtable, not a silent bug, which is fortunate. */

#include "ofx_internal.h"

#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "tilly/containers.h"

/* ------------------------------------------------ handle translation */

/* Resolves an OfxImageEffectHandle to the object it denotes. OFX uses one
 * opaque pointer for both a plugin descriptor (describe) and an instance
 * (describe-in-context onwards), so the handle is classified by its tag instead
 * of being cast blindly: casting a plugin descriptor to an instance reads
 * unrelated bytes as a property-bag count. */
jfx_ofx_instance_t *jfx_ofx_instance_from_handle(const void *handle) {
    if (!handle) return NULL;
    const jfx_ofx_owner_t *owner = (const jfx_ofx_owner_t *)handle;
    if (owner->magic != JFX_OFX_HANDLE_MAGIC) return NULL;
    if (owner->kind != JFX_OFX_OWNER_INSTANCE) return NULL;
    return (jfx_ofx_instance_t *)handle;
}

jfx_ofx_plugin_slot_t *jfx_ofx_plugin_from_handle(const void *handle) {
    if (!handle) return NULL;
    const jfx_ofx_owner_t *owner = (const jfx_ofx_owner_t *)handle;
    if (owner->magic != JFX_OFX_HANDLE_MAGIC) return NULL;
    if (owner->kind != JFX_OFX_OWNER_PLUGIN) return NULL;
    return (jfx_ofx_plugin_slot_t *)handle;
}

jfx_ofx_param_t *jfx_ofx_param_from_handle(const void *handle) {
    return (jfx_ofx_param_t *)handle;
}

jfx_ofx_clip_t *jfx_ofx_clip_from_handle(const void *handle) {
    return (jfx_ofx_clip_t *)handle;
}

jfx_ofx_instance_t *jfx_ofx_owner_of(const void *handle) {
    if (!handle) return NULL;
    jfx_ofx_param_t *param = jfx_ofx_param_from_handle(handle);
    return param ? param->owner : NULL;
}

/* ------------------------------------------------- parameter suite vtable */

/* Component count implied by an OFX parameter type. Anything not listed is a
 * scalar, which also covers the valueless Group/Page parameters. */
static int components_for_type(const char *paramType) {
    if (!paramType) return 1;
    if (!strcmp(paramType, kOfxParamTypeRGB)) return 3;
    if (!strcmp(paramType, kOfxParamTypeRGBA)) return 4;
    if (!strcmp(paramType, kOfxParamTypeDouble2D)) return 2;
    if (!strcmp(paramType, kOfxParamTypeInteger2D)) return 2;
    if (!strcmp(paramType, kOfxParamTypeDouble3D)) return 3;
    if (!strcmp(paramType, kOfxParamTypeInteger3D)) return 3;
    return 1;
}

static OfxStatus param_define(OfxParamSetHandle paramSet,
    const char *paramType, const char *name,
    OfxPropertySetHandle *propertySet) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(paramSet);
    if (!instance || !paramType || !name || !propertySet) {
        return kOfxStatErrValue;
    }
    if (instance->param_count >= JFX_OFX_MAX_PARAMS) return kOfxStatErrMemory;
    if (!name[0] || strlen(name) >= JFX_OFX_MAX_NAME) return kOfxStatErrValue;

    jfx_ofx_param_t *param = &instance->params[instance->param_count++];
    memset(param, 0, sizeof(*param));
    param->owner = instance;
    snprintf(param->name, sizeof(param->name), "%s", name);
    jfx_ofx_props_init(&param->properties);

    /* The returned handle must answer to the property suite, so it is the
     * param's own description bag: the plugin writes default/min/max/label onto
     * it and resolves the parameter later by name. */
    *propertySet = (OfxPropertySetHandle)&param->properties;
    param->component_count = components_for_type(paramType);
    return kOfxStatOK;
}

static OfxStatus param_get_handle(OfxParamSetHandle paramSet, const char *name,
    OfxParamHandle *param, OfxPropertySetHandle *propertySet) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(paramSet);
    if (!instance || !name || !param) return kOfxStatErrValue;
    for (int i = 0; i < instance->param_count; ++i) {
        if (strcmp(instance->params[i].name, name) != 0) continue;
        *param = (OfxParamHandle)&instance->params[i];
        /* The trailing property-set output is optional in OFX; supply the
         * parameter's description bag when the caller asks for it. */
        if (propertySet)
            *propertySet = (OfxPropertySetHandle)&instance->params[i].properties;
        return kOfxStatOK;
    }
    return kOfxStatErrValue;
}

static OfxStatus param_set_get_property_set(OfxParamSetHandle paramSet,
    OfxPropertySetHandle *propHandle) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(paramSet);
    if (!instance || !propHandle) return kOfxStatErrValue;
    *propHandle = (OfxPropertySetHandle)&instance->instance_properties;
    return kOfxStatOK;
}

static OfxStatus param_get_property_set(OfxParamHandle param,
    OfxPropertySetHandle *propHandle) {
    jfx_ofx_param_t *entry = jfx_ofx_param_from_handle(param);
    if (!entry || !propHandle) return kOfxStatErrValue;
    *propHandle = (OfxPropertySetHandle)&entry->properties;
    return kOfxStatOK;
}

/* Clamp to the declared range, as OFX requires of hosts. A parameter with no
 * declared minimum/maximum passes through unchanged. Shared with the public
 * setters in ofx_host.c so both paths clamp identically. */
double jfx_ofx_clamp_param(const jfx_ofx_param_t *param, double value) {
    double minimum = 0.0, maximum = 0.0;
    int has_min = jfx_ofx_props_get_number(&param->properties,
        kOfxParamPropMin, 0, &minimum);
    int has_max = jfx_ofx_props_get_number(&param->properties,
        kOfxParamPropMax, 0, &maximum);
    if (!isfinite(value)) return has_min ? minimum : 0.0;
    if (has_min && value < minimum) return minimum;
    if (has_max && value > maximum) return maximum;
    return value;
}

static OfxStatus param_get_value(OfxParamHandle paramHandle, ...) {
    jfx_ofx_param_t *param = jfx_ofx_param_from_handle(paramHandle);
    if (!param) return kOfxStatErrValue;
    va_list args;
    va_start(args, paramHandle);
    /* OFX passes a pointer to the caller's value: double* for scalar
     * parameters, or one pointer per component for colour/point/RGBA. */
    double *out = va_arg(args, double *);
    va_end(args);
    if (!out) return kOfxStatErrValue;
    *out = param->scalar;
    return kOfxStatOK;
}

static OfxStatus param_set_value(OfxParamHandle paramHandle, ...) {
    jfx_ofx_param_t *param = jfx_ofx_param_from_handle(paramHandle);
    if (!param) return kOfxStatErrValue;
    va_list args;
    va_start(args, paramHandle);
    double *in = va_arg(args, double *);
    va_end(args);
    if (!in) return kOfxStatErrValue;
    param->scalar = jfx_ofx_clamp_param(param, *in);
    param->components[0] = param->scalar;
    return kOfxStatOK;
}

/* This host is non-animated: a value at any time is the value at every time.
 * The signature is variadic because OFX declares it so; the value pointer
 * arrives through `...`, so va_start names the last fixed parameter. */
static OfxStatus param_get_value_at_time(OfxParamHandle paramHandle,
    OfxTime time, ...) {
    jfx_ofx_param_t *param = jfx_ofx_param_from_handle(paramHandle);
    if (!param) return kOfxStatErrValue;
    va_list args;
    va_start(args, time);
    double *out = va_arg(args, double *);
    va_end(args);
    if (!out) return kOfxStatErrValue;
    *out = param->scalar;
    return kOfxStatOK;
}

static OfxStatus param_get_derivative(OfxParamHandle paramHandle,
    OfxTime time, ...) {
    (void)paramHandle; (void)time;
    return kOfxStatReplyDefault; /* No animation, so no derivative. */
}

static OfxStatus param_get_integral(OfxParamHandle paramHandle,
    OfxTime time1, OfxTime time2, ...) {
    (void)paramHandle; (void)time1; (void)time2;
    return kOfxStatReplyDefault;
}

static OfxStatus param_set_value_at_time(OfxParamHandle paramHandle,
    OfxTime time, ...) {
    (void)paramHandle; (void)time;
    return kOfxStatReplyDefault;
}

static OfxStatus param_get_num_keys(OfxParamHandle paramHandle,
    unsigned int *numberOfKeys) {
    (void)paramHandle;
    if (!numberOfKeys) return kOfxStatErrValue;
    *numberOfKeys = 0;
    return kOfxStatOK;
}

static OfxStatus param_get_key_time(OfxParamHandle paramHandle,
    unsigned int nthKey, OfxTime *time) {
    (void)paramHandle; (void)nthKey;
    if (!time) return kOfxStatErrValue;
    return kOfxStatErrValue; /* No keys exist. */
}

static OfxStatus param_get_key_index(OfxParamHandle paramHandle, OfxTime time,
    int direction, int *index) {
    (void)paramHandle; (void)time; (void)direction;
    if (!index) return kOfxStatErrValue;
    *index = -1;
    return kOfxStatOK;
}

static OfxStatus param_delete_key(OfxParamHandle paramHandle, OfxTime time) {
    (void)paramHandle; (void)time;
    return kOfxStatReplyDefault; /* Non-animated host: nothing to delete. */
}

static OfxStatus param_delete_all_keys(OfxParamHandle paramHandle) {
    (void)paramHandle;
    return kOfxStatReplyDefault;
}

static OfxStatus param_copy(OfxParamHandle paramTo, OfxParamHandle paramFrom,
    OfxTime dstOffset, const OfxRangeD *frameRange) {
    (void)dstOffset; (void)frameRange;
    jfx_ofx_param_t *to = jfx_ofx_param_from_handle(paramTo);
    jfx_ofx_param_t *from = jfx_ofx_param_from_handle(paramFrom);
    if (!to || !from) return kOfxStatErrValue;
    to->scalar = from->scalar;
    return kOfxStatOK;
}

static OfxStatus param_edit_begin(OfxParamSetHandle paramSet, const char *name) {
    /* Single-threaded host: the edit bracket is implicit and there is nothing
     * to snapshot, since a value persists until overwritten. */
    (void)jfx_ofx_instance_from_handle(paramSet);
    (void)name;
    return kOfxStatOK;
}

static OfxStatus param_edit_end(OfxParamSetHandle paramSet) {
    (void)jfx_ofx_instance_from_handle(paramSet);
    return kOfxStatOK;
}

static void install_parameter_suite(jfx_ofx_host_t *host) {
    OfxParameterSuiteV1 *s = &host->parameter_suite;
    memset(s, 0, sizeof(*s));
    s->paramDefine = param_define;
    s->paramGetHandle = param_get_handle;
    s->paramSetGetPropertySet = param_set_get_property_set;
    s->paramGetPropertySet = param_get_property_set;
    s->paramGetValue = param_get_value;
    s->paramSetValue = param_set_value;
    s->paramGetValueAtTime = param_get_value_at_time;
    s->paramSetValueAtTime = param_set_value_at_time;
    s->paramGetDerivative = param_get_derivative;
    s->paramGetIntegral = param_get_integral;
    s->paramGetNumKeys = param_get_num_keys;
    s->paramGetKeyTime = param_get_key_time;
    s->paramGetKeyIndex = param_get_key_index;
    s->paramDeleteKey = param_delete_key;
    s->paramDeleteAllKeys = param_delete_all_keys;
    s->paramCopy = param_copy;
    s->paramEditBegin = param_edit_begin;
    s->paramEditEnd = param_edit_end;
}

/* ------------------------------------------------ image effect suite vtable */

/* getPropertySet is called with a plugin descriptor during describe and with an
 * instance afterwards, so it resolves whichever the handle denotes. */
static OfxStatus ie_get_property_set(OfxImageEffectHandle imageEffect,
    OfxPropertySetHandle *propHandle) {
    if (!propHandle) return kOfxStatErrValue;
    jfx_ofx_instance_t *instance =
        jfx_ofx_instance_from_handle(imageEffect);
    if (instance) {
        *propHandle = (OfxPropertySetHandle)&instance->instance_properties;
        return kOfxStatOK;
    }
    jfx_ofx_plugin_slot_t *plugin = jfx_ofx_plugin_from_handle(imageEffect);
    if (plugin) {
        *propHandle = (OfxPropertySetHandle)&plugin->effect_properties;
        return kOfxStatOK;
    }
    return kOfxStatErrValue;
}

static OfxStatus ie_get_param_set(OfxImageEffectHandle imageEffect,
    OfxParamSetHandle *paramSet) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(imageEffect);
    if (!instance || !paramSet) return kOfxStatErrValue;
    /* The paramSet handle is the instance, which is what paramDefine and
     * paramGetHandle resolve through. */
    *paramSet = (OfxParamSetHandle)instance;
    return kOfxStatOK;
}

static OfxStatus ie_clip_define(OfxImageEffectHandle imageEffect,
    const char *name, OfxPropertySetHandle *propertySet) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(imageEffect);
    if (!instance || !name || !propertySet) return kOfxStatErrValue;
    if (instance->clip_count >= JFX_OFX_MAX_CLIPS) return kOfxStatErrMemory;
    if (!name[0] || strlen(name) >= JFX_OFX_MAX_NAME) return kOfxStatErrValue;

    jfx_ofx_clip_t *clip = &instance->clips[instance->clip_count++];
    memset(clip, 0, sizeof(*clip));
    clip->owner = instance;
    snprintf(clip->name, sizeof(clip->name), "%s", name);
    clip->is_input = strcmp(name, kOfxImageEffectOutputClipName) != 0;
    jfx_ofx_props_init(&clip->properties);
    jfx_ofx_props_init(&clip->image_properties);
    clip->components = 4;
    *propertySet = (OfxPropertySetHandle)&clip->properties;
    return kOfxStatOK;
}

static OfxStatus ie_clip_get_handle(OfxImageEffectHandle imageEffect,
    const char *name, OfxImageClipHandle *clip,
    OfxPropertySetHandle *propertySet) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(imageEffect);
    if (!instance || !name || !clip) return kOfxStatErrValue;
    for (int i = 0; i < instance->clip_count; ++i) {
        if (strcmp(instance->clips[i].name, name) != 0) continue;
        *clip = (OfxImageClipHandle)&instance->clips[i];
        if (propertySet)
            *propertySet = (OfxPropertySetHandle)&instance->clips[i].properties;
        return kOfxStatOK;
    }
    return kOfxStatErrValue;
}

static OfxStatus ie_clip_get_property_set(OfxImageClipHandle clip,
    OfxPropertySetHandle *propHandle) {
    jfx_ofx_clip_t *entry = jfx_ofx_clip_from_handle(clip);
    if (!entry || !propHandle) return kOfxStatErrValue;
    *propHandle = (OfxPropertySetHandle)&entry->properties;
    return kOfxStatOK;
}

/* Publishes the clip's current buffer as an image property set. Pixels are
 * borrowed, never copied: the caller owns the source frame and the destination
 * buffer for the duration of the render. */
static OfxStatus publish_image(jfx_ofx_clip_t *clip) {
    if (!clip || !clip->pixels || !clip->width || !clip->height)
        return kOfxStatErrValue;
    clip->row_bytes = clip->width * (size_t)clip->components * sizeof(float);
    jfx_ofx_props_init(&clip->image_properties);
    jfx_ofx_props_set_string(&clip->image_properties,
        kOfxImageEffectPropPixelDepth, kOfxBitDepthFloat);
    jfx_ofx_props_set_string(&clip->image_properties,
        kOfxImageEffectPropComponents,
        clip->components == 4 ? kOfxImageComponentRGBA : kOfxImageComponentRGB);
    jfx_ofx_props_set_pointer(&clip->image_properties, kOfxImagePropData,
        clip->pixels);
    jfx_ofx_props_set_int(&clip->image_properties, kOfxImagePropRowBytes,
        (int)clip->row_bytes);
    int bounds[4] = { 0, 0, (int)clip->width, (int)clip->height };
    jfx_ofx_props_set_int_array(&clip->image_properties, kOfxImagePropBounds,
        bounds, 4);
    double par[2] = { 1.0, 1.0 };
    jfx_ofx_props_set_double_array(&clip->image_properties,
        kOfxImagePropPixelAspectRatio, par, 2);
    clip->valid = 1;
    clip->released = 0;
    return kOfxStatOK;
}

static OfxStatus ie_clip_get_image(OfxImageClipHandle clip, OfxTime time,
    const OfxRectD *region, OfxPropertySetHandle *imageHandle) {
    (void)time; /* Non-temporal host: one image serves every time. */
    (void)region; /* Non-tiled host: the full buffer satisfies any region. */
    jfx_ofx_clip_t *entry = jfx_ofx_clip_from_handle(clip);
    if (!entry || !imageHandle) return kOfxStatErrValue;
    OfxStatus status = publish_image(entry);
    if (status != kOfxStatOK) return status;
    *imageHandle = (OfxPropertySetHandle)&entry->image_properties;
    return kOfxStatOK;
}

static OfxStatus ie_clip_release_image(OfxPropertySetHandle imageHandle) {
    if (!imageHandle) return kOfxStatErrValue;
    /* Images are borrowed views onto caller memory, so releasing is only a flag
     * flip. Recover the owning clip: the image bag is embedded in it, so the
     * container_of offset is exact. */
    jfx_ofx_clip_t *entry = (jfx_ofx_clip_t *)((char *)imageHandle
        - offsetof(jfx_ofx_clip_t, image_properties));
    entry->valid = 0;
    entry->released = 1;
    return kOfxStatOK;
}

static OfxStatus ie_clip_get_region_of_definition(OfxImageClipHandle clip,
    OfxTime time, OfxRectD *bounds) {
    (void)time;
    jfx_ofx_clip_t *entry = jfx_ofx_clip_from_handle(clip);
    if (!entry || !bounds) return kOfxStatErrValue;
    if (!entry->pixels || !entry->width || !entry->height)
        return kOfxStatErrValue;
    bounds->x1 = 0.0;
    bounds->y1 = 0.0;
    bounds->x2 = (double)entry->width;
    bounds->y2 = (double)entry->height;
    return kOfxStatOK;
}

static int ie_abort(OfxImageEffectHandle imageEffect) {
    jfx_ofx_instance_t *instance = jfx_ofx_instance_from_handle(imageEffect);
    if (instance) instance->aborted = 1;
    return instance ? 0 : 1;
}

/* Scratch allocation for plugins. Uses the engine's allocator rather than
 * malloc, per the repository rule that allocation goes through Tilly. */
static OfxStatus ie_image_memory_alloc(OfxImageEffectHandle instanceHandle,
    size_t nBytes, OfxImageMemoryHandle *memoryHandle) {
    (void)instanceHandle;
    if (!nBytes || !memoryHandle) return kOfxStatErrValue;
    void *memory = tilly_container_alloc(nBytes);
    if (!memory) return kOfxStatErrMemory;
    *memoryHandle = (OfxImageMemoryHandle)memory;
    return kOfxStatOK;
}

static OfxStatus ie_image_memory_free(OfxImageMemoryHandle memoryHandle) {
    if (!memoryHandle) return kOfxStatErrValue;
    tilly_container_free(memoryHandle);
    return kOfxStatOK;
}

static OfxStatus ie_image_memory_lock(OfxImageMemoryHandle memoryHandle,
    void **returnedPtr) {
    if (!memoryHandle || !returnedPtr) return kOfxStatErrValue;
    *returnedPtr = memoryHandle;
    return kOfxStatOK;
}

static OfxStatus ie_image_memory_unlock(OfxImageMemoryHandle memoryHandle) {
    (void)memoryHandle;
    return kOfxStatOK;
}

static void install_image_suite(jfx_ofx_host_t *host) {
    OfxImageEffectSuiteV1 *s = &host->image_suite;
    memset(s, 0, sizeof(*s));
    s->getPropertySet = ie_get_property_set;
    s->getParamSet = ie_get_param_set;
    s->clipDefine = ie_clip_define;
    s->clipGetHandle = ie_clip_get_handle;
    s->clipGetPropertySet = ie_clip_get_property_set;
    s->clipGetImage = ie_clip_get_image;
    s->clipReleaseImage = ie_clip_release_image;
    s->clipGetRegionOfDefinition = ie_clip_get_region_of_definition;
    s->abort = ie_abort;
    s->imageMemoryAlloc = ie_image_memory_alloc;
    s->imageMemoryFree = ie_image_memory_free;
    s->imageMemoryLock = ie_image_memory_lock;
    s->imageMemoryUnlock = ie_image_memory_unlock;
}

void jfx_ofx_install_suites(jfx_ofx_host_t *host) {
    if (!host) return;
    jfx_ofx_install_property_suite(host);
    install_parameter_suite(host);
    install_image_suite(host);
}