/* OFX property bags and the OfxPropertySuiteV1 implementation.
 *
 * Properties are the backbone of OFX: suites, clips, parameters, images and the
 * per-action argument sets are all property sets. This file provides the bag
 * itself plus the vtable plugins call into.
 *
 * Every entry point validates its handle and index. A plugin that passes an
 * unknown property, a negative index, or an out-of-range dimension gets an
 * error status rather than a crash, because plugin binaries are third-party
 * code loaded from the filesystem. */

#include "ofx_internal.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ bags */

void jfx_ofx_props_init(jfx_ofx_property_set_t *set) {
    if (set) memset(set, 0, sizeof(*set));
}

void jfx_ofx_props_dispose(jfx_ofx_property_set_t *set) {
    /* Values are scalars, fixed-capacity buffers or borrowed pointers, so there
     * is nothing to release; clear the bag so a stale pointer cannot be mistaken
     * for live data. */
    if (set) memset(set, 0, sizeof(*set));
}

jfx_ofx_property_t *jfx_ofx_props_find(jfx_ofx_property_set_t *set,
    const char *name) {
    if (!set || !name) return NULL;
    for (int i = 0; i < set->count; ++i)
        if (strcmp(set->items[i].name, name) == 0) return &set->items[i];
    return NULL;
}

void jfx_ofx_props_remove_prefix(jfx_ofx_property_set_t *set,
    const char *prefix) {
    if (!set || !prefix) return;
    size_t n = strlen(prefix);
    int out = 0;
    for (int i = 0; i < set->count; ++i) {
        if (strncmp(set->items[i].name, prefix, n) == 0) continue;
        if (out != i) set->items[out] = set->items[i];
        ++out;
    }
    set->count = out;
}

/* Returns the property for `name`, creating it when `create` is set. Creation
 * fails (NULL) when the bag is full or the name does not fit. */
static jfx_ofx_property_t *slot(jfx_ofx_property_set_t *set, const char *name,
    int create) {
    if (!set || !name || !name[0]) return NULL;
    jfx_ofx_property_t *existing = jfx_ofx_props_find(set, name);
    if (existing || !create) return existing;
    if (set->count >= JFX_OFX_MAX_PROPERTIES) {
        set->overflowed = 1;
        return NULL;
    }
    if (strlen(name) >= JFX_OFX_MAX_NAME) {
        set->overflowed = 1;
        return NULL;
    }
    jfx_ofx_property_t *p = &set->items[set->count++];
    memset(p, 0, sizeof(*p));
    snprintf(p->name, sizeof(p->name), "%s", name);
    p->type = JFX_OFX_VALUE_NONE;
    return p;
}

int jfx_ofx_props_set_int(jfx_ofx_property_set_t *set, const char *name,
    int value) {
    jfx_ofx_property_t *p = slot(set, name, 1);
    if (!p) return 0;
    p->type = JFX_OFX_VALUE_INT;
    p->dimension = 1;
    p->ints[0] = value;
    return 1;
}

int jfx_ofx_props_set_double(jfx_ofx_property_set_t *set, const char *name,
    double value) {
    jfx_ofx_property_t *p = slot(set, name, 1);
    if (!p) return 0;
    p->type = JFX_OFX_VALUE_DOUBLE;
    p->dimension = 1;
    p->doubles[0] = value;
    return 1;
}

int jfx_ofx_props_set_pointer(jfx_ofx_property_set_t *set, const char *name,
    void *value) {
    jfx_ofx_property_t *p = slot(set, name, 1);
    if (!p) return 0;
    p->type = JFX_OFX_VALUE_POINTER;
    p->dimension = 1;
    p->pointer = value;
    return 1;
}

int jfx_ofx_props_set_int_array(jfx_ofx_property_set_t *set, const char *name,
    const int *values, int count) {
    if (count < 0 || count > JFX_OFX_MAX_PROPERTY_DIMENSION) return 0;
    jfx_ofx_property_t *p = slot(set, name, 1);
    if (!p) return 0;
    p->type = JFX_OFX_VALUE_INT;
    p->dimension = count;
    for (int i = 0; i < count; ++i) p->ints[i] = values ? values[i] : 0;
    return 1;
}

int jfx_ofx_props_set_double_array(jfx_ofx_property_set_t *set, const char *name,
    const double *values, int count) {
    if (count < 0 || count > JFX_OFX_MAX_PROPERTY_DIMENSION) return 0;
    jfx_ofx_property_t *p = slot(set, name, 1);
    if (!p) return 0;
    p->type = JFX_OFX_VALUE_DOUBLE;
    p->dimension = count;
    for (int i = 0; i < count; ++i) p->doubles[i] = values ? values[i] : 0.0;
    return 1;
}

/* Writes entry `index` of a string-valued property. Growing arrays one index at
 * a time is how plugins fill the supported-context and supported-components
 * lists, so the dimension grows to cover the highest index written. */
int jfx_ofx_props_set_string_at(jfx_ofx_property_set_t *set, const char *name,
    int index, const char *value) {
    if (index < 0 || index >= JFX_OFX_MAX_STRING_ARRAY) return 0;
    if (!value) return 0;
    if (strlen(value) >= JFX_OFX_STRING_BYTES) {
        if (set) set->overflowed = 1;
        return 0;
    }
    jfx_ofx_property_t *p = slot(set, name, 1);
    if (!p) return 0;
    p->type = JFX_OFX_VALUE_STRING;
    snprintf(p->strings[index], JFX_OFX_STRING_BYTES, "%s", value);
    if (index + 1 > p->dimension) p->dimension = index + 1;
    return 1;
}

int jfx_ofx_props_set_string(jfx_ofx_property_set_t *set, const char *name,
    const char *value) {
    return jfx_ofx_props_set_string_at(set, name, 0, value);
}

int jfx_ofx_props_get_int(const jfx_ofx_property_set_t *set, const char *name,
    int index, int *out_value) {
    if (!out_value || index < 0) return 0;
    jfx_ofx_property_t *p =
        jfx_ofx_props_find((jfx_ofx_property_set_t *)set, name);
    if (!p || p->type != JFX_OFX_VALUE_INT || index >= p->dimension) return 0;
    *out_value = p->ints[index];
    return 1;
}

int jfx_ofx_props_get_double(const jfx_ofx_property_set_t *set, const char *name,
    int index, double *out_value) {
    if (!out_value || index < 0) return 0;
    jfx_ofx_property_t *p =
        jfx_ofx_props_find((jfx_ofx_property_set_t *)set, name);
    if (!p || p->type != JFX_OFX_VALUE_DOUBLE || index >= p->dimension) return 0;
    *out_value = p->doubles[index];
    return 1;
}

int jfx_ofx_props_get_number(const jfx_ofx_property_set_t *set, const char *name,
    int index, double *out_value) {
    if (!out_value || index < 0) return 0;
    jfx_ofx_property_t *p =
        jfx_ofx_props_find((jfx_ofx_property_set_t *)set, name);
    if (!p || index >= p->dimension) return 0;
    if (p->type == JFX_OFX_VALUE_DOUBLE) {
        *out_value = p->doubles[index];
        return 1;
    }
    if (p->type == JFX_OFX_VALUE_INT) {
        *out_value = (double)p->ints[index];
        return 1;
    }
    return 0;
}

const char *jfx_ofx_props_get_string(const jfx_ofx_property_set_t *set,
    const char *name, int index) {
    if (index < 0) return NULL;
    jfx_ofx_property_t *p =
        jfx_ofx_props_find((jfx_ofx_property_set_t *)set, name);
    if (!p || p->type != JFX_OFX_VALUE_STRING || index >= p->dimension)
        return NULL;
    return p->strings[index];
}

int jfx_ofx_props_get_dimension(const jfx_ofx_property_set_t *set,
    const char *name) {
    jfx_ofx_property_t *p =
        jfx_ofx_props_find((jfx_ofx_property_set_t *)set, name);
    return p ? p->dimension : 0;
}

/* --------------------------------------------------- property suite vtable */

/* The suite receives raw OfxPropertySetHandle values. Casting through the bag
 * type is safe because every handle this host hands out is a
 * jfx_ofx_property_set_t, and these entry points are only reachable through the
 * suites published by jfx_ofx_install_suites(). */
static jfx_ofx_property_set_t *as_props(OfxPropertySetHandle handle) {
    return (jfx_ofx_property_set_t *)handle;
}

static OfxStatus prop_set_pointer(OfxPropertySetHandle properties,
    const char *property, int index, void *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || index != 0) return kOfxStatErrValue;
    return jfx_ofx_props_set_pointer(set, property, value) ? kOfxStatOK
                                                           : kOfxStatErrMemory;
}

static OfxStatus prop_set_string(OfxPropertySetHandle properties,
    const char *property, int index, const char *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property) return kOfxStatErrValue;
    return jfx_ofx_props_set_string_at(set, property, index, value)
        ? kOfxStatOK : kOfxStatErrMemory;
}

static OfxStatus prop_set_double(OfxPropertySetHandle properties,
    const char *property, int index, double value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || index != 0) return kOfxStatErrValue;
    return jfx_ofx_props_set_double(set, property, value) ? kOfxStatOK
                                                          : kOfxStatErrMemory;
}

static OfxStatus prop_set_int(OfxPropertySetHandle properties,
    const char *property, int index, int value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || index != 0) return kOfxStatErrValue;
    return jfx_ofx_props_set_int(set, property, value) ? kOfxStatOK
                                                       : kOfxStatErrMemory;
}

/* Pointer arrays are stored as a single pointer plus a dimension. No OFX image
 * effect property this host publishes needs more than one pointer per name. */
static OfxStatus prop_set_pointer_n(OfxPropertySetHandle properties,
    const char *property, int count, void *const *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || count < 0 || count > 1) return kOfxStatErrValue;
    return prop_set_pointer(properties, property, 0, count > 0 && value
        ? value[0] : NULL);
}

static OfxStatus prop_set_string_n(OfxPropertySetHandle properties,
    const char *property, int count, const char *const *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || !value || count < 0) return kOfxStatErrValue;
    if (count > JFX_OFX_MAX_STRING_ARRAY) return kOfxStatErrValue;
    /* Clear first so a shorter rewrite does not leave stale trailing entries. */
    jfx_ofx_property_t *p = jfx_ofx_props_find(set, property);
    if (p) p->dimension = 0;
    for (int i = 0; i < count; ++i)
        if (!jfx_ofx_props_set_string_at(set, property, i, value[i]))
            return kOfxStatErrMemory;
    return kOfxStatOK;
}

static OfxStatus prop_set_double_n(OfxPropertySetHandle properties,
    const char *property, int count, const double *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || count < 0) return kOfxStatErrValue;
    return jfx_ofx_props_set_double_array(set, property, value, count)
        ? kOfxStatOK : kOfxStatErrMemory;
}

static OfxStatus prop_set_int_n(OfxPropertySetHandle properties,
    const char *property, int count, const int *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || count < 0) return kOfxStatErrValue;
    return jfx_ofx_props_set_int_array(set, property, value, count)
        ? kOfxStatOK : kOfxStatErrMemory;
}

static OfxStatus prop_get_pointer(OfxPropertySetHandle properties,
    const char *property, int index, void **value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || !value || index != 0) return kOfxStatErrValue;
    jfx_ofx_property_t *p = jfx_ofx_props_find(set, property);
    if (!p || p->type != JFX_OFX_VALUE_POINTER) return kOfxStatErrValue;
    *value = p->pointer;
    return kOfxStatOK;
}

static OfxStatus prop_get_string(OfxPropertySetHandle properties,
    const char *property, int index, char **value) {
    const char *text =
        jfx_ofx_props_get_string(as_props(properties), property, index);
    if (!text || !value) return kOfxStatErrValue;
    *value = (char *)text;
    return kOfxStatOK;
}

static OfxStatus prop_get_double(OfxPropertySetHandle properties,
    const char *property, int index, double *value) {
    if (!jfx_ofx_props_get_double(as_props(properties), property, index, value))
        return kOfxStatErrValue;
    return kOfxStatOK;
}

static OfxStatus prop_get_int(OfxPropertySetHandle properties,
    const char *property, int index, int *value) {
    if (!jfx_ofx_props_get_int(as_props(properties), property, index, value))
        return kOfxStatErrValue;
    return kOfxStatOK;
}

static OfxStatus prop_get_pointer_n(OfxPropertySetHandle properties,
    const char *property, int count, void **value) {
    if (!value || count < 1) return kOfxStatErrValue;
    OfxStatus status = prop_get_pointer(properties, property, 0, value);
    if (status != kOfxStatOK) return status;
    for (int i = 1; i < count; ++i) value[i] = NULL;
    return kOfxStatOK;
}

static OfxStatus prop_get_string_n(OfxPropertySetHandle properties,
    const char *property, int count, char **value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!value || count < 1) return kOfxStatErrValue;
    jfx_ofx_property_t *p = jfx_ofx_props_find(set, property);
    if (!p || p->type != JFX_OFX_VALUE_STRING) return kOfxStatErrValue;
    for (int i = 0; i < count; ++i)
        value[i] = i < p->dimension ? (char *)p->strings[i] : NULL;
    return kOfxStatOK;
}

static OfxStatus prop_get_double_n(OfxPropertySetHandle properties,
    const char *property, int count, double *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!value || count < 0) return kOfxStatErrValue;
    jfx_ofx_property_t *p = jfx_ofx_props_find(set, property);
    if (!p || p->type != JFX_OFX_VALUE_DOUBLE) return kOfxStatErrValue;
    for (int i = 0; i < count; ++i)
        value[i] = i < p->dimension ? p->doubles[i] : 0.0;
    return kOfxStatOK;
}

static OfxStatus prop_get_int_n(OfxPropertySetHandle properties,
    const char *property, int count, int *value) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!value || count < 0) return kOfxStatErrValue;
    jfx_ofx_property_t *p = jfx_ofx_props_find(set, property);
    if (!p || p->type != JFX_OFX_VALUE_INT) return kOfxStatErrValue;
    for (int i = 0; i < count; ++i)
        value[i] = i < p->dimension ? p->ints[i] : 0;
    return kOfxStatOK;
}

static OfxStatus prop_reset(OfxPropertySetHandle properties,
    const char *property) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property) return kOfxStatErrValue;
    jfx_ofx_property_t *p = jfx_ofx_props_find(set, property);
    if (!p) return kOfxStatErrValue;
    memset(p, 0, sizeof(*p));
    return kOfxStatOK;
}

static OfxStatus prop_get_dimension(OfxPropertySetHandle properties,
    const char *property, int *count) {
    jfx_ofx_property_set_t *set = as_props(properties);
    if (!set || !property || !count) return kOfxStatErrValue;
    *count = jfx_ofx_props_get_dimension(set, property);
    return kOfxStatOK;
}

void jfx_ofx_install_property_suite(jfx_ofx_host_t *host) {
    OfxPropertySuiteV1 *s = &host->property_suite;
    memset(s, 0, sizeof(*s));
    s->propSetPointer = prop_set_pointer;
    s->propSetString = prop_set_string;
    s->propSetDouble = prop_set_double;
    s->propSetInt = prop_set_int;
    s->propSetPointerN = prop_set_pointer_n;
    s->propSetStringN = prop_set_string_n;
    s->propSetDoubleN = prop_set_double_n;
    s->propSetIntN = prop_set_int_n;
    s->propGetPointer = prop_get_pointer;
    s->propGetString = prop_get_string;
    s->propGetDouble = prop_get_double;
    s->propGetInt = prop_get_int;
    s->propGetPointerN = prop_get_pointer_n;
    s->propGetStringN = prop_get_string_n;
    s->propGetDoubleN = prop_get_double_n;
    s->propGetIntN = prop_get_int_n;
    s->propReset = prop_reset;
    s->propGetDimension = prop_get_dimension;
}