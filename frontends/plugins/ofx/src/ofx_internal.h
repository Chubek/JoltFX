#ifndef JFX_OFX_INTERNAL_H
#define JFX_OFX_INTERNAL_H

/* Internal layout for the OFX host adapter. Not installed and not part of the
 * public contract: every type here exists only to back an OFX handle. */

#include <stddef.h>
#include <stdint.h>

#include "jfx/jfx_ofx.h"

/* OFX's own headers. These define the suite structs the plugin calls back into,
 * so the adapter must agree with them exactly. */
#include "ofxCore.h"
#include "ofxImageEffect.h"
#include "ofxParam.h"
#include "ofxProperty.h"

/* Bounds on everything a plugin can make us allocate, so a malformed or
 * hostile bundle cannot exhaust the host. These are deliberately small: a
 * property bag is embedded in every clip and parameter, so the per-property
 * footprint is multiplied by JFX_OFX_MAX_CLIPS and JFX_OFX_MAX_PARAMS on
 * every instance. */
#define JFX_OFX_MAX_PROPERTIES 24
#define JFX_OFX_MAX_PROPERTY_DIMENSION 8
/* Entries in a string-valued property, e.g. kOfxImageEffectPropSupportedContexts
 * or kOfxImageEffectPropSupportedComponents, which plugins write per index. */
#define JFX_OFX_MAX_STRING_ARRAY 4
#define JFX_OFX_STRING_BYTES 128
#define JFX_OFX_MAX_CLIPS 8
#define JFX_OFX_MAX_PARAMS 32
#define JFX_OFX_MAX_NAME 96
#define JFX_OFX_MAX_INSTANCES 64
#define JFX_OFX_MAX_IMAGE_DIMENSION 65536u
#define JFX_OFX_MAX_PLUGINS 64

typedef enum {
    JFX_OFX_VALUE_NONE = 0,
    JFX_OFX_VALUE_INT,
    JFX_OFX_VALUE_DOUBLE,
    JFX_OFX_VALUE_STRING,
    JFX_OFX_VALUE_POINTER
} jfx_ofx_value_type_t;

/* One OFX property: a named, optionally multi-component value. OFX properties
 * are sparse and grow as a plugin describes itself, so this is a flat array
 * with linear lookup. Payloads are fixed-capacity so a single oversized
 * property cannot become a plugin-sized heap allocation. */
typedef struct {
    char name[JFX_OFX_MAX_NAME];
    jfx_ofx_value_type_t type;
    int dimension;
    double doubles[JFX_OFX_MAX_PROPERTY_DIMENSION];
    int ints[JFX_OFX_MAX_PROPERTY_DIMENSION];
    char strings[JFX_OFX_MAX_STRING_ARRAY][JFX_OFX_STRING_BYTES];
    void *pointer;
} jfx_ofx_property_t;

typedef struct {
    jfx_ofx_property_t items[JFX_OFX_MAX_PROPERTIES];
    int count;
    /* Set when the plugin wrote a property this host cannot represent, so the
     * action can be failed rather than silently returning a default. */
    int overflowed;
} jfx_ofx_property_set_t;

/* Clips are declared by the plugin in describe-in-context and requested during
 * render. A clip's image is either owned by the caller (the source frame the
 * host supplies) or by us (the destination buffer). */
typedef struct {
    char name[JFX_OFX_MAX_NAME];
    int is_input;
    /* Owning instance. OFX hands clipGetImage/clipGetPropertySet only the clip
     * handle, so the instance has to be reachable from the clip. */
    struct jfx_ofx_instance *owner;
    jfx_ofx_property_set_t properties;
    jfx_ofx_property_set_t image_properties;
    /* Borrowed for input clips, caller-owned for output. Never freed here. */
    float *pixels;
    size_t width;
    size_t height;
    size_t row_bytes;
    int components; /* 3 for RGB, 4 for RGBA. */
    int released;
    int valid;
} jfx_ofx_clip_t;

typedef struct {
    char name[JFX_OFX_MAX_NAME];
    /* Owning instance. OFX hands paramGetValue/paramSetValue only the param
     * handle, so the instance has to be reachable from the parameter. */
    struct jfx_ofx_instance *owner;
    /* The parameter's description properties (label, hint, default, min, max).
     * This bag is also the OfxPropertySetHandle handed back by paramDefine. */
    jfx_ofx_property_set_t properties;
    double scalar;
    double components[4];
    int component_count;
} jfx_ofx_param_t;

/* OFX uses one opaque `void *` handle for two different things: during
 * kOfxActionDescribe it is a plugin descriptor, and from describe-in-context
 * onwards it is an instance. Both arrive at the same suite entry points, so the
 * handle must be classified before use. Every object that can be passed as an
 * OfxImageEffectHandle therefore starts with one of these tags; a handle that is
 * NULL, or that does not carry a known tag, is rejected rather than cast. */
#define JFX_OFX_HANDLE_MAGIC 0x4a465857u /* "JFXW" */

typedef enum {
    JFX_OFX_OWNER_NONE = 0,
    JFX_OFX_OWNER_PLUGIN = 1,
    JFX_OFX_OWNER_INSTANCE = 2
} jfx_ofx_owner_kind_t;

typedef struct {
    uint32_t magic;
    jfx_ofx_owner_kind_t kind;
    jfx_ofx_host_t *host;
} jfx_ofx_owner_t;

struct jfx_ofx_plugin_slot {
    jfx_ofx_owner_t owner;
    jfx_ofx_plugin_info_t info;
    void *library; /* dlopen handle; NULL for a statically attached plugin. */
    OfxPlugin *plugin;
    jfx_ofx_context_t context;
    int described;
    unsigned supports_contexts;
    int instance_count;
    /* Per-plugin host properties. During kOfxActionDescribe the plugin calls
     * getPropertySet on the plugin handle and writes its supported contexts,
     * pixel depths and label here, so this handle is passed as the
     * OfxImageEffectHandle for the describe action. */
    jfx_ofx_property_set_t effect_properties;
};
typedef struct jfx_ofx_plugin_slot jfx_ofx_plugin_slot_t;

struct jfx_ofx_instance {
    jfx_ofx_owner_t owner;
    jfx_ofx_host_t *host;
    jfx_ofx_plugin_slot_t *plugin;
    jfx_ofx_context_t context;
    int created;
    int aborted;
    jfx_ofx_clip_t clips[JFX_OFX_MAX_CLIPS];
    int clip_count;
    jfx_ofx_param_t params[JFX_OFX_MAX_PARAMS];
    int param_count;
    jfx_ofx_property_set_t instance_properties;
    jfx_ofx_instance_capabilities_t capabilities;
    char error_message[256];
};

struct jfx_ofx_host {
    jfx_ofx_host_desc_t desc;
    char bundle_directory[JFX_OFX_MAX_PATH_LENGTH + 1u];
    jfx_ofx_plugin_slot_t *plugins;
    size_t plugin_count;
    size_t plugin_capacity;
    int instance_count;
    /* Global host capabilities, read by plugins during describe. This bag is the
     * OfxPropertySetHandle published through OfxHost::host. */
    jfx_ofx_property_set_t properties;
    /* Suites handed to plugins. One instance each, shared by every plugin in
     * the host, as OFX recommends. */
    OfxPropertySuiteV1 property_suite;
    OfxParameterSuiteV1 parameter_suite;
    OfxImageEffectSuiteV1 image_suite;
    OfxHost ofx_host;
};

/* Property set helpers (ofx_property.c). */
void jfx_ofx_props_init(jfx_ofx_property_set_t *set);
void jfx_ofx_props_dispose(jfx_ofx_property_set_t *set);
jfx_ofx_property_t *jfx_ofx_props_find(jfx_ofx_property_set_t *set,
    const char *name);
/* Removes every property whose name starts with `prefix`; OFX uses "Ofx"
 * prefixes for namespacing. */
void jfx_ofx_props_remove_prefix(jfx_ofx_property_set_t *set, const char *prefix);
int jfx_ofx_props_set_int(jfx_ofx_property_set_t *set, const char *name, int value);
int jfx_ofx_props_set_double(jfx_ofx_property_set_t *set, const char *name,
    double value);
int jfx_ofx_props_set_string(jfx_ofx_property_set_t *set, const char *name,
    const char *value);
int jfx_ofx_props_set_int_array(jfx_ofx_property_set_t *set, const char *name,
    const int *values, int count);
int jfx_ofx_props_set_double_array(jfx_ofx_property_set_t *set, const char *name,
    const double *values, int count);
int jfx_ofx_props_set_pointer(jfx_ofx_property_set_t *set, const char *name,
    void *value);
/* Writes one entry of a string-array property, e.g. the supported-context or
 * supported-components lists, which plugins fill by index. */
int jfx_ofx_props_set_string_at(jfx_ofx_property_set_t *set, const char *name,
    int index, const char *value);
int jfx_ofx_props_get_int(const jfx_ofx_property_set_t *set, const char *name,
    int index, int *out_value);
int jfx_ofx_props_get_double(const jfx_ofx_property_set_t *set, const char *name,
    int index, double *out_value);
/* Reads a numeric property written as either an int or a double. OFX plugins
 * legitimately write kOfxParamPropDefault as an int for boolean/integer
 * parameters and as a double for floating-point ones. */
int jfx_ofx_props_get_number(const jfx_ofx_property_set_t *set, const char *name,
    int index, double *out_value);
const char *jfx_ofx_props_get_string(const jfx_ofx_property_set_t *set,
    const char *name, int index);
int jfx_ofx_props_get_dimension(const jfx_ofx_property_set_t *set,
    const char *name);

/* Suite vtable installation (ofx_property.c, ofx_suites.c). */
void jfx_ofx_install_property_suite(jfx_ofx_host_t *host);
void jfx_ofx_install_suites(jfx_ofx_host_t *host);
/* Shared clamping to a parameter's declared min/max (ofx_suites.c), reused by
 * the public setters so both paths agree. */
double jfx_ofx_clamp_param(const jfx_ofx_param_t *param, double value);
jfx_ofx_instance_t *jfx_ofx_instance_from_handle(const void *handle);
jfx_ofx_plugin_slot_t *jfx_ofx_plugin_from_handle(const void *handle);
/* Looks up a param/clip by OFX handle. Returns NULL when the handle was not
 * produced by this host, which is how a plugin passing a stale or foreign
 * handle is rejected. */
jfx_ofx_param_t *jfx_ofx_param_from_handle(const void *handle);
jfx_ofx_clip_t *jfx_ofx_clip_from_handle(const void *handle);
/* The instance a handle belongs to, or NULL if unrecognised. */
jfx_ofx_instance_t *jfx_ofx_owner_of(const void *handle);

#endif /* JFX_OFX_INTERNAL_H */