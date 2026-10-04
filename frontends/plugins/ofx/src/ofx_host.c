/* OFX host adapter: plugin discovery, the action lifecycle, and the render
 * bridge that hands plugin output back as float RGBA.
 *
 * The action sequence implemented here is the OFX 1.5 image-effect protocol:
 *
 *   load -> describe -> describe-in-context -> create-instance -> render
 *        -> destroy
 *
 * describe receives the plugin handle as its OfxImageEffectHandle, because a
 * plugin legitimately calls getPropertySet on it to publish its supported
 * contexts, pixel depths and label. Passing NULL there, as a literal reading of
 * "the handle is redundant", breaks every real plugin.
 *
 * Only the actions a CPU, non-tiled, non-animated host needs are trapped. The
 * host advertises no tiles, no multi-resolution, no temporal clip access and no
 * animation, which is what keeps a plugin inside what can actually be serviced
 * here rather than failing later at render time. */

#include "ofx_internal.h"

#include <dirent.h>
#include <stddef.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "tilly/containers.h"

#if defined(_WIN32)
#include <windows.h>
#define JFX_OFX_DLOPEN(path) LoadLibraryA(path)
#define JFX_OFX_DLSYM(handle, symbol) \
    ((void *)GetProcAddress((HMODULE)(handle), (symbol)))
#define JFX_OFX_DLCLOSE(handle) FreeLibrary((HMODULE)(handle))
#else
#include <dlfcn.h>
#include <unistd.h>
#define JFX_OFX_DLOPEN(path) dlopen((path), RTLD_NOW | RTLD_LOCAL)
#define JFX_OFX_DLSYM(handle, symbol) dlsym((handle), (symbol))
#define JFX_OFX_DLCLOSE(handle) dlclose((handle))
#endif

typedef int (*ofx_get_number_fn)(void);
typedef OfxPlugin *(*ofx_get_plugin_fn)(int);

#define JFX_OFX_API_VERSION 1

/* Bit for one context, or 0 for a value outside the enumeration. Taking the
 * context as an int and validating here keeps the shift well-defined and avoids
 * an implicit int-to-unsigned conversion at every call site. */
static unsigned context_bit(int context) {
    if (context < 0 || context > (int)JFX_OFX_CONTEXT_GENERAL) return 0;
    return 1u << (unsigned)context;
}

static const char *default_bundle_directory(void) {
#if defined(_WIN32)
    static char buffer[JFX_OFX_MAX_PATH_LENGTH + 1u];
    const char *common = getenv("COMMONPROGRAMFILES");
    if (common) {
        snprintf(buffer, sizeof(buffer), "%s\\OFX\\Plugins", common);
        return buffer;
    }
    return "C:\\Program Files\\Common Files\\OFX\\Plugins";
#elif defined(__APPLE__)
    return "/Library/OFX/Plugins";
#else
    return "/usr/OFX/Plugins";
#endif
}

/* The OfxHost the plugin calls back through. Suites are fetched by name and
 * version; anything else returns NULL, which is how a plugin learns this host
 * does not implement, say, OpenGL rendering or animation.
 *
 * OFX passes the *global host property handle* to fetchSuite, not the host
 * struct, so the host is recovered from the embedded property bag with an exact
 * container_of offset. Casting that handle straight to jfx_ofx_host_t would read
 * the suite vtables from the wrong offset and hand the plugin garbage. */
static const void *fetch_suite(OfxPropertySetHandle host_handle,
    const char *suite_name, int suite_version) {
    if (!host_handle || !suite_name || suite_version != 1) return NULL;
    jfx_ofx_host_t *host = (jfx_ofx_host_t *)((char *)host_handle
        - offsetof(struct jfx_ofx_host, properties));
    if (!strcmp(suite_name, kOfxPropertySuite)) return &host->property_suite;
    if (!strcmp(suite_name, kOfxParameterSuite)) return &host->parameter_suite;
    if (!strcmp(suite_name, kOfxImageEffectSuite)) return &host->image_suite;
    /* Deliberately absent: time, interact, message, progress, memory, threading,
     * parametric-parameter and the GPU/OpenGL suites. */
    return NULL;
}

static const char *context_name(jfx_ofx_context_t context) {
    switch (context) {
    case JFX_OFX_CONTEXT_FILTER: return kOfxImageEffectContextFilter;
    case JFX_OFX_CONTEXT_GENERATOR: return kOfxImageEffectContextGenerator;
    case JFX_OFX_CONTEXT_TRANSITION: return kOfxImageEffectContextTransition;
    case JFX_OFX_CONTEXT_PAINT: return kOfxImageEffectContextPaint;
    case JFX_OFX_CONTEXT_RETIMER: return kOfxImageEffectContextRetimer;
    case JFX_OFX_CONTEXT_GENERAL: return kOfxImageEffectContextGeneral;
    default: return NULL;
    }
}

static int context_from_name(const char *name) {
    if (!name) return -1;
    for (int c = 0; c <= JFX_OFX_CONTEXT_GENERAL; ++c) {
        const char *candidate = context_name((jfx_ofx_context_t)c);
        if (candidate && !strcmp(candidate, name)) return c;
    }
    return -1;
}

/* Global host capabilities a plugin reads during describe. Declaring float RGBA
 * only, with no tiling, no multi-resolution and no temporal access, is what
 * bounds what this host will later be asked to service. */
static void fill_host_properties(jfx_ofx_property_set_t *set) {
    jfx_ofx_props_init(set);
    jfx_ofx_props_set_int(set, kOfxPropType, 1);
    jfx_ofx_props_set_int(set, kOfxPropAPIVersion, JFX_OFX_API_VERSION);
    jfx_ofx_props_set_int(set, kOfxPropVersion, 0x00010000); /* 1.0 */
    jfx_ofx_props_set_int(set, kOfxImageEffectPropSupportsMultiResolution, 0);
    jfx_ofx_props_set_int(set, kOfxImageEffectPropSupportsTiles, 0);
    jfx_ofx_props_set_int(set, kOfxImageEffectPropTemporalClipAccess, 0);
    jfx_ofx_props_set_int(set, kOfxImageEffectPropSupportsOverlays, 0);
    jfx_ofx_props_set_int(set, kOfxImageEffectPropSupportsMultipleClipDepths, 0);
    jfx_ofx_props_set_int(set, kOfxImageEffectPropSupportsMultipleClipPARs, 0);
    jfx_ofx_props_set_int(set, kOfxParamHostPropSupportsCustomAnimation, 0);
    jfx_ofx_props_set_int(set, kOfxParamHostPropSupportsStringAnimation, 0);
    jfx_ofx_props_set_int(set, kOfxParamHostPropSupportsBooleanAnimation, 0);
    jfx_ofx_props_set_int(set, kOfxParamHostPropSupportsChoiceAnimation, 0);
    jfx_ofx_props_set_int(set, kOfxParamHostPropSupportsCustomInteract, 0);
    jfx_ofx_props_set_int(set, kOfxParamHostPropMaxParameters, 
        JFX_OFX_MAX_PARAMS);
}

/* ------------------------------------------------------------- host object */

jfx_result_t jfx_ofx_host_create(const jfx_ofx_host_desc_t *desc,
    jfx_ofx_host_t **out_host) {
    if (!out_host) return JFX_ERROR_INVALID_ARGUMENT;
    *out_host = NULL;
    if (desc && desc->size != sizeof(*desc)) return JFX_ERROR_INVALID_ARGUMENT;

    jfx_ofx_host_t *host = tilly_container_calloc(1, sizeof(*host));
    if (!host) return JFX_ERROR_OUT_OF_MEMORY;

    if (desc) host->desc = *desc;
    if (host->desc.max_instances_per_plugin == 0)
        host->desc.max_instances_per_plugin = 8;
    if (host->desc.max_instances_per_plugin > JFX_OFX_MAX_INSTANCES)
        host->desc.max_instances_per_plugin = JFX_OFX_MAX_INSTANCES;

    const char *directory = host->desc.bundle_directory
        ? host->desc.bundle_directory : default_bundle_directory();
    snprintf(host->bundle_directory, sizeof(host->bundle_directory), "%s",
        directory);

    jfx_ofx_install_suites(host);
    fill_host_properties(&host->properties);
    host->ofx_host.host = (OfxPropertySetHandle)&host->properties;
    host->ofx_host.fetchSuite = fetch_suite;

    *out_host = host;
    return JFX_SUCCESS;
}

void jfx_ofx_host_destroy(jfx_ofx_host_t *host) {
    if (!host) return;
    /* Only the first slot of a module owns the library handle; see
     * attach_library for why several plugins can share one module. */
    for (size_t i = 0; i < host->plugin_count; ++i) {
        jfx_ofx_plugin_slot_t *slot = &host->plugins[i];
        if (slot->library) JFX_OFX_DLCLOSE(slot->library);
        slot->library = NULL;
        slot->plugin = NULL;
    }
    tilly_container_free(host->plugins);
    tilly_container_free(host);
}

/* ------------------------------------------------------------- discovery */

/* OFX bundles are <Name>.ofx.bundle/Contents/<platform>/<Name>.ofx. One bundle
 * may ship a binary per platform, so only the running platform's is loaded. */
static void platform_subdirectory(char *out, size_t capacity) {
#if defined(__APPLE__)
#if defined(__aarch64__) || defined(__arm64__)
    snprintf(out, capacity, "MacOS/arm64");
#else
    snprintf(out, capacity, "MacOS/x86-64");
#endif
#elif defined(_WIN32)
#if defined(_M_ARM64) || defined(__aarch64__)
    snprintf(out, capacity, "Windows/arm64");
#else
    snprintf(out, capacity, "Windows/x86-64");
#endif
#else
    snprintf(out, capacity, "Linux-x86-64");
#endif
}

/* Joins path components with '/' separators, refusing to truncate: a silently
 * shortened path could name a different bundle, so overflow is reported and the
 * caller skips the candidate. `c` may be NULL for a two-component join. */
static int join_path(char *out, size_t capacity, const char *a, const char *b,
    const char *c) {
    const char *parts[3];
    size_t lengths[3];
    int count = 0;
    parts[count] = a; lengths[count] = strlen(a); ++count;
    if (b) { parts[count] = b; lengths[count] = strlen(b); ++count; }
    if (c) { parts[count] = c; lengths[count] = strlen(c); ++count; }
    /* Measure everything before writing anything, so `out` may alias one of the
     * inputs: no source byte is read after the corresponding output byte is
     * overwritten. */
    size_t total = 0;
    for (int i = 0; i < count; ++i) total += lengths[i] + (i ? 1u : 0u);
    if (total >= capacity) return 0;
    size_t used = 0;
    for (int i = 0; i < count; ++i) {
        if (i) out[used++] = '/';
        memmove(out + used, parts[i], lengths[i]);
        used += lengths[i];
    }
    out[used] = 0;
    return 1;
}

/* Runs load then describe and records the contexts the plugin declared.
 * A plugin that fails either is not usable here and is dropped. */
static jfx_result_t load_and_describe(jfx_ofx_plugin_slot_t *slot) {
    OfxPlugin *plugin = slot->plugin;
    if (!plugin->mainEntry) return JFX_ERROR_PLUGIN_FAILURE;

    OfxStatus status = plugin->mainEntry(kOfxActionLoad, NULL, NULL, NULL);
    if (status != kOfxStatOK && status != kOfxStatReplyDefault)
        return JFX_ERROR_PLUGIN_FAILURE;

    /* describe: the plugin resolves its effect property set from this handle
     * and fills in the contexts it supports, one entry per index. */
    jfx_ofx_props_init(&slot->effect_properties);
    status = plugin->mainEntry(kOfxActionDescribe, slot, NULL,
        (OfxPropertySetHandle)&slot->effect_properties);
    if (status != kOfxStatOK && status != kOfxStatReplyDefault)
        return JFX_ERROR_PLUGIN_FAILURE;

    unsigned supported = 0;
    int dimensions =
        jfx_ofx_props_get_dimension(&slot->effect_properties,
            kOfxImageEffectPropSupportedContexts);
    for (int i = 0; i < dimensions; ++i) {
        const char *name = jfx_ofx_props_get_string(&slot->effect_properties,
            kOfxImageEffectPropSupportedContexts, i);
        int context = context_from_name(name);
        /* An unrecognised context name is skipped rather than guessed at. */
        if (context >= 0) supported |= context_bit(context);
    }
    if (!supported) {
        /* Some plugins report the context in outArgs of a describe call made
         * with a fresh bag. Re-run once against a scratch bag so a host that
         * supports exactly one context still works with them. */
        jfx_ofx_property_set_t scratch;
        jfx_ofx_props_init(&scratch);
        if (plugin->mainEntry(kOfxActionDescribe, slot, NULL,
                (OfxPropertySetHandle)&scratch) == kOfxStatOK) {
            const char *name = jfx_ofx_props_get_string(&scratch,
                kOfxImageEffectPropContext, 0);
            int context = context_from_name(name);
            if (context >= 0) supported |= context_bit(context);
        }
    }
    if (!supported) return JFX_ERROR_PLUGIN_FAILURE;

    const char *label = jfx_ofx_props_get_string(&slot->effect_properties,
        kOfxPropLabel, 0);
    if (label) snprintf(slot->info.name, sizeof(slot->info.name), "%s", label);

    slot->supports_contexts = supported;
    slot->info.supported_contexts = supported;
    slot->described = 1;
    return JFX_SUCCESS;
}

/* Attaches every image-effect plugin exported by one loaded module. */
static jfx_result_t attach_library(jfx_ofx_host_t *host, void *library,
    const char *reported_path) {
    /* POSIX specifies dlsym as returning an object pointer that the caller
     * converts to a function pointer; ISO C has no such conversion, so the
     * project warning is suppressed for these two lines only. The alternative
     * is a memcpy through a union, which is no more portable. */
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
    ofx_get_number_fn get_number =
        (ofx_get_number_fn)JFX_OFX_DLSYM(library, "OfxGetNumberOfPlugins");
    ofx_get_plugin_fn get_plugin =
        (ofx_get_plugin_fn)JFX_OFX_DLSYM(library, "OfxGetPlugin");
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif
    if (!get_number || !get_plugin) return JFX_ERROR_PLUGIN_FAILURE;

    int count = get_number();
    if (count <= 0 || count > JFX_OFX_MAX_PLUGINS) return JFX_ERROR_PLUGIN_FAILURE;

    int attached = 0;
    for (int i = 0; i < count; ++i) {
        if (host->plugin_count >= JFX_OFX_MAX_PLUGINS) break;
        OfxPlugin *plugin = get_plugin(i);
        if (!plugin || !plugin->pluginApi || !plugin->mainEntry
            || !plugin->pluginIdentifier) continue;
        /* This host implements only the image-effect API, at version 1. */
        if (strcmp(plugin->pluginApi, kOfxImageEffectPluginApi) != 0) continue;
        if (plugin->apiVersion != JFX_OFX_API_VERSION) continue;

        /* Reserve the slot before it is taken: the array starts NULL, so
         * indexing it first would be a null dereference on the first plugin. */
        if (host->plugin_count == host->plugin_capacity) {
            size_t next = host->plugin_capacity
                ? host->plugin_capacity * 2 : 8;
            if (next > JFX_OFX_MAX_PLUGINS) next = JFX_OFX_MAX_PLUGINS;
            void *grown = tilly_container_realloc(host->plugins,
                next * sizeof(*host->plugins));
            if (!grown) return attached ? JFX_SUCCESS : JFX_ERROR_OUT_OF_MEMORY;
            host->plugins = grown;
            memset(host->plugins + host->plugin_capacity, 0,
                (next - host->plugin_capacity) * sizeof(*host->plugins));
            host->plugin_capacity = next;
        }

        jfx_ofx_plugin_slot_t *slot = &host->plugins[host->plugin_count];
        memset(slot, 0, sizeof(*slot));
        /* Tag the handle before setHost, so a plugin that calls back into the
         * suite during describe is classified as a plugin descriptor. */
        slot->owner.magic = JFX_OFX_HANDLE_MAGIC;
        slot->owner.kind = JFX_OFX_OWNER_PLUGIN;
        slot->owner.host = host;
        /* Several plugins may live in one module. Only the first claims the
         * library handle so host destruction closes it exactly once; the rest
         * record NULL and are skipped if this module is rescanned. */
        slot->library = attached ? NULL : library;
        slot->plugin = plugin;
        snprintf(slot->info.identifier, sizeof(slot->info.identifier), "%s",
            plugin->pluginIdentifier);
        snprintf(slot->info.name, sizeof(slot->info.name), "%s",
            plugin->pluginIdentifier);
        slot->info.version_major = plugin->pluginVersionMajor;
        slot->info.version_minor = plugin->pluginVersionMinor;
        if (reported_path && attached == 0)
            snprintf(slot->info.bundle_path, sizeof(slot->info.bundle_path),
                "%s", reported_path);

        /* setHost must precede every other call, and the host pointer the
         * plugin keeps is only valid while this binary stays loaded. */
        if (!plugin->setHost) continue;
        plugin->setHost(&host->ofx_host);

        /* A plugin that fails load or describe is dropped, leaving the slot
         * unused so the next candidate reuses it. */
        if (load_and_describe(slot) != JFX_SUCCESS) continue;

        ++host->plugin_count;
        ++attached;
    }
    /* Every slot in this module failed, so the caller still owns the handle. */
    return attached ? JFX_SUCCESS : JFX_ERROR_PLUGIN_FAILURE;
}

jfx_result_t jfx_ofx_host_scan(jfx_ofx_host_t *host, const char *directory) {
    if (!host) return JFX_ERROR_INVALID_ARGUMENT;
    const char *root = directory ? directory : host->bundle_directory;

    /* Resolve to an absolute path so recorded bundle paths stay valid. */
    char resolved[JFX_OFX_MAX_PATH_LENGTH + 1u];
#if defined(_WIN32)
    if (_fullpath(resolved, root, sizeof(resolved)) == NULL)
        return JFX_ERROR_NOT_FOUND;
#else
    if (!realpath(root, resolved)) return JFX_ERROR_NOT_FOUND;
#endif

    char platform[64];
    platform_subdirectory(platform, sizeof(platform));

    DIR *dir = opendir(resolved);

    /* Collect names first: the host's plugin array must not grow while an open
     * directory handle is live. */
    static char names[64][JFX_OFX_MAX_NAME + 8];
    int name_count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && name_count < 64) {
        const char *n = entry->d_name;
        if (n[0] == '.') continue;
        /* A name too long for the buffer cannot be a bundle directory whose
         * binary we could then address, so it is dropped rather than truncated
         * into a different path. */
        if (strlen(n) >= sizeof(names[0])) continue;
        memcpy(names[name_count], n, strlen(n) + 1);
        ++name_count;
    }
    closedir(dir);

    for (int i = 0; i < name_count; ++i) {
        /* The suffix test applies to the directory name, and the binary inside
         * is named after the plugin: Foo.ofx.bundle/Contents/<platform>/Foo.ofx.
         * Using the directory name verbatim would look for Foo.ofx.bundle.ofx
         * and silently load nothing. */
        static const char suffix[] = ".ofx.bundle";
        const size_t suffix_length = sizeof(suffix) - 1;
        size_t name_length = strlen(names[i]);
        if (name_length <= suffix_length) continue;
        if (strcmp(names[i] + name_length - suffix_length, suffix) != 0) continue;

        char bundle[JFX_OFX_MAX_PATH_LENGTH + 1u];
        if (!join_path(bundle, sizeof(bundle), resolved, names[i], NULL))
            continue;
        struct stat st;
        if (stat(bundle, &st) != 0 || !S_ISDIR(st.st_mode)) continue;

        char plugin_name[JFX_OFX_MAX_NAME];
        snprintf(plugin_name, sizeof(plugin_name), "%.*s",
            (int)(name_length - suffix_length), names[i]);
        char leaf[JFX_OFX_MAX_NAME + 8u];
        if ((size_t)snprintf(leaf, sizeof(leaf), "%s.ofx", plugin_name)
            >= sizeof(leaf)) continue;

        char binary[JFX_OFX_MAX_PATH_LENGTH + 1u];
        char platform_dir[JFX_OFX_MAX_PATH_LENGTH + 1u];
        if (!join_path(platform_dir, sizeof(platform_dir), bundle, "Contents",
                platform)) continue;
        if (!join_path(binary, sizeof(binary), platform_dir, leaf, NULL))
            continue;
        if (stat(binary, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        void *library = JFX_OFX_DLOPEN(binary);
        if (!library) continue;
        if (attach_library(host, library, binary) != JFX_SUCCESS)
            JFX_OFX_DLCLOSE(library);
    }
    /* Finding no bundles is not an error: the standard plugin directories are
     * frequently empty and a caller may scan several paths in a row. */
    return JFX_SUCCESS;
}

size_t jfx_ofx_host_plugin_count(const jfx_ofx_host_t *host) {
    return host ? host->plugin_count : 0;
}

jfx_result_t jfx_ofx_host_plugin_info(const jfx_ofx_host_t *host, size_t index,
    jfx_ofx_plugin_info_t *out_info) {
    if (!host || !out_info) return JFX_ERROR_INVALID_ARGUMENT;
    if (out_info->size != sizeof(*out_info)) return JFX_ERROR_INVALID_ARGUMENT;
    if (index >= host->plugin_count) return JFX_ERROR_NOT_FOUND;
    *out_info = host->plugins[index].info;
    return JFX_SUCCESS;
}

/* ------------------------------------------------------------- instances */

static jfx_ofx_clip_t *find_clip(jfx_ofx_instance_t *instance,
    const char *name) {
    for (int i = 0; i < instance->clip_count; ++i)
        if (strcmp(instance->clips[i].name, name) == 0)
            return &instance->clips[i];
    return NULL;
}

static jfx_ofx_param_t *find_param(jfx_ofx_instance_t *instance,
    const char *name) {
    for (int i = 0; i < instance->param_count; ++i)
        if (strcmp(instance->params[i].name, name) == 0)
            return &instance->params[i];
    return NULL;
}

jfx_result_t jfx_ofx_instance_create(jfx_ofx_host_t *host, size_t plugin_index,
    const jfx_ofx_instance_desc_t *desc, jfx_ofx_instance_t **out_instance) {
    if (!host || !desc || !out_instance) return JFX_ERROR_INVALID_ARGUMENT;
    *out_instance = NULL;
    if (desc->size != sizeof(*desc)) return JFX_ERROR_INVALID_ARGUMENT;
    if (plugin_index >= host->plugin_count) return JFX_ERROR_NOT_FOUND;
    if (host->instance_count >= JFX_OFX_MAX_INSTANCES) return JFX_ERROR_BUSY;

    jfx_ofx_plugin_slot_t *slot = &host->plugins[plugin_index];
    if (!slot->described) return JFX_ERROR_NOT_INITIALIZED;
    if (!(slot->supports_contexts & context_bit((int)desc->context)))
        return JFX_ERROR_INVALID_ARGUMENT;
    if (slot->instance_count >= (int)host->desc.max_instances_per_plugin)
        return JFX_ERROR_BUSY;

    const char *name = context_name(desc->context);
    if (!name) return JFX_ERROR_INVALID_ARGUMENT;

    jfx_ofx_instance_t *instance = tilly_container_calloc(1, sizeof(*instance));
    if (!instance) return JFX_ERROR_OUT_OF_MEMORY;
    instance->owner.magic = JFX_OFX_HANDLE_MAGIC;
    instance->owner.kind = JFX_OFX_OWNER_INSTANCE;
    instance->owner.host = host;
    instance->host = host;
    instance->plugin = slot;
    instance->context = desc->context;
    jfx_ofx_props_init(&instance->instance_properties);

    /* describe-in-context: the plugin declares this context's clips and
     * parameters. inArgs carries the context; outArgs is unused. */
    jfx_ofx_property_set_t in;
    jfx_ofx_props_init(&in);
    jfx_ofx_props_set_string(&in, kOfxImageEffectPropContext, name);
    jfx_ofx_property_set_t out;
    jfx_ofx_props_init(&out);
    OfxStatus status = slot->plugin->mainEntry(
        kOfxImageEffectActionDescribeInContext, instance,
        (OfxPropertySetHandle)&in, (OfxPropertySetHandle)&out);
    if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
        tilly_container_free(instance);
        return JFX_ERROR_PLUGIN_FAILURE;
    }
    /* Without an output clip there is nowhere to render into. */
    if (!find_clip(instance, kOfxImageEffectOutputClipName)) {
        tilly_container_free(instance);
        return JFX_ERROR_PLUGIN_FAILURE;
    }

    /* create-instance. */
    jfx_ofx_property_set_t args;
    jfx_ofx_props_init(&args);
    status = slot->plugin->mainEntry(kOfxActionCreateInstance, instance,
        (OfxPropertySetHandle)&args, (OfxPropertySetHandle)&args);
    if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
        tilly_container_free(instance);
        return JFX_ERROR_PLUGIN_FAILURE;
    }
    instance->created = 1;

    /* Seed values from the declared defaults. Plugins write kOfxParamPropDefault
     * as an int for boolean/integer parameters and a double otherwise. */
    for (int i = 0; i < instance->param_count; ++i) {
        jfx_ofx_param_t *param = &instance->params[i];
        double def = 0.0;
        if (jfx_ofx_props_get_number(&param->properties, kOfxParamPropDefault, 0,
                &def)) {
            param->scalar = def;
            param->components[0] = def;
        }
    }

    instance->capabilities.size = sizeof(instance->capabilities);
    instance->capabilities.context = desc->context;
    /* Restate what the host actually provides for this instance. */
    instance->capabilities.supports_multi_resolution = 0;
    instance->capabilities.supports_tiles = 0;
    instance->capabilities.temporal_clip_access = 0;
    instance->capabilities.supports_overlays = 0;

    slot->instance_count++;
    host->instance_count++;
    *out_instance = instance;
    return JFX_SUCCESS;
}

void jfx_ofx_instance_destroy(jfx_ofx_instance_t *instance) {
    if (!instance) return;
    if (instance->plugin && instance->created) {
        jfx_ofx_property_set_t args;
        jfx_ofx_props_init(&args);
        instance->plugin->plugin->mainEntry(kOfxActionDestroyInstance, instance,
            (OfxPropertySetHandle)&args, (OfxPropertySetHandle)&args);
    }
    if (instance->plugin) instance->plugin->instance_count--;
    if (instance->host) instance->host->instance_count--;
    tilly_container_free(instance);
}

jfx_result_t jfx_ofx_instance_capabilities(const jfx_ofx_instance_t *instance,
    jfx_ofx_instance_capabilities_t *out_capabilities) {
    if (!instance || !out_capabilities) return JFX_ERROR_INVALID_ARGUMENT;
    if (out_capabilities->size != sizeof(*out_capabilities))
        return JFX_ERROR_INVALID_ARGUMENT;
    *out_capabilities = instance->capabilities;
    return JFX_SUCCESS;
}

size_t jfx_ofx_instance_param_count(const jfx_ofx_instance_t *instance) {
    return instance ? (size_t)instance->param_count : 0;
}

/* Recovers the public parameter type. The description bag records the OFX type
 * only implicitly, through the dimension the plugin declared plus whether it
 * published a label; component count is the reliable signal, so scalar
 * parameters are reported as doubles, which is what the vast majority are. */
static jfx_ofx_param_type_t param_type_of(const jfx_ofx_param_t *param) {
    switch (param->component_count) {
    case 4: return JFX_OFX_PARAM_RGBA;
    case 3: return JFX_OFX_PARAM_COLOR;
    case 2: return JFX_OFX_PARAM_POINT;
    default: return JFX_OFX_PARAM_DOUBLE;
    }
}

jfx_result_t jfx_ofx_instance_param_info(const jfx_ofx_instance_t *instance,
    size_t index, jfx_ofx_param_info_t *out_info) {
    if (!instance || !out_info) return JFX_ERROR_INVALID_ARGUMENT;
    if (out_info->size != sizeof(*out_info)) return JFX_ERROR_INVALID_ARGUMENT;
    if (index >= (size_t)instance->param_count) return JFX_ERROR_NOT_FOUND;

    const jfx_ofx_param_t *param = &instance->params[index];
    memset(out_info, 0, sizeof(*out_info));
    snprintf(out_info->name, sizeof(out_info->name), "%s", param->name);
    /* OFX 1.5 has no separate label property for a parameter; kOfxPropLabel is
     * what plugins set on the parameter's property set, and the hint is the
     * fallback description. */
    const char *label = jfx_ofx_props_get_string(&param->properties,
        kOfxPropLabel, 0);
    if (!label)
        label = jfx_ofx_props_get_string(&param->properties, kOfxParamPropHint, 0);
    snprintf(out_info->label, sizeof(out_info->label), "%s",
        label ? label : param->name);
    out_info->type = param_type_of(param);
    out_info->component_count = param->component_count;
    if (!jfx_ofx_props_get_number(&param->properties, kOfxParamPropDefault, 0,
            &out_info->default_value))
        out_info->default_value = param->scalar;
    /* An absent bound is reported as the current value, which makes the
     * reported range a no-op rather than a clamp the caller would misread. */
    if (!jfx_ofx_props_get_number(&param->properties, kOfxParamPropMin, 0,
            &out_info->minimum))
        out_info->minimum = param->scalar;
    if (!jfx_ofx_props_get_number(&param->properties, kOfxParamPropMax, 0,
            &out_info->maximum))
        out_info->maximum = param->scalar;
    int animates = 0;
    if (jfx_ofx_props_get_int(&param->properties, kOfxParamPropAnimates, 0,
            &animates))
        out_info->animatable = animates;
    return JFX_SUCCESS;
}

jfx_result_t jfx_ofx_instance_set_param(jfx_ofx_instance_t *instance,
    const char *name, double value) {
    if (!instance || !name) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_ofx_param_t *param = find_param(instance, name);
    if (!param) return JFX_ERROR_NOT_FOUND;
    param->scalar = jfx_ofx_clamp_param(param, value);
    param->components[0] = param->scalar;
    return JFX_SUCCESS;
}

jfx_result_t jfx_ofx_instance_get_param(const jfx_ofx_instance_t *instance,
    const char *name, double *out_value) {
    if (!instance || !name || !out_value) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_ofx_param_t *param = find_param((jfx_ofx_instance_t *)instance, name);
    if (!param) return JFX_ERROR_NOT_FOUND;
    *out_value = param->scalar;
    return JFX_SUCCESS;
}

jfx_result_t jfx_ofx_instance_set_param_components(jfx_ofx_instance_t *instance,
    const char *name, const double *values, int count) {
    if (!instance || !name || !values) return JFX_ERROR_INVALID_ARGUMENT;
    if (count < 1 || count > 4) return JFX_ERROR_INVALID_ARGUMENT;
    jfx_ofx_param_t *param = find_param(instance, name);
    if (!param) return JFX_ERROR_NOT_FOUND;
    if (param->component_count > 1 && count != param->component_count)
        return JFX_ERROR_INVALID_ARGUMENT;
    for (int i = 0; i < count; ++i)
        param->components[i] = jfx_ofx_clamp_param(param, values[i]);
    param->component_count = count;
    param->scalar = param->components[0];
    return JFX_SUCCESS;
}

jfx_result_t jfx_ofx_instance_get_param_components(
    const jfx_ofx_instance_t *instance, const char *name, double *out_values,
    int capacity, int *out_count) {
    if (!instance || !name || !out_values || !out_count) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (capacity < 1) return JFX_ERROR_INVALID_ARGUMENT;
    const jfx_ofx_param_t *param =
        find_param((jfx_ofx_instance_t *)instance, name);
    if (!param) return JFX_ERROR_NOT_FOUND;
    int n = param->component_count;
    if (n > capacity) n = capacity;
    for (int i = 0; i < n; ++i) out_values[i] = param->components[i];
    *out_count = n;
    return JFX_SUCCESS;
}

const char *jfx_ofx_instance_last_error(const jfx_ofx_instance_t *instance) {
    if (!instance || !instance->error_message[0]) return NULL;
    return instance->error_message;
}

/* ---------------------------------------------------------------- render */

jfx_result_t jfx_ofx_instance_render(jfx_ofx_instance_t *instance,
    const jfx_ofx_render_desc_t *desc) {
    if (!instance || !desc || !desc->dst) return JFX_ERROR_INVALID_ARGUMENT;
    if (desc->size != sizeof(*desc)) return JFX_ERROR_INVALID_ARGUMENT;
    size_t width = desc->width, height = desc->height;
    if (!width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    if (width > JFX_OFX_MAX_IMAGE_DIMENSION
        || height > JFX_OFX_MAX_IMAGE_DIMENSION) return JFX_ERROR_INVALID_ARGUMENT;
    if (!instance->created) return JFX_ERROR_NOT_INITIALIZED;
    if (instance->aborted) return JFX_ERROR_BUSY;
    /* Guard the multiplication below before it is used as an allocation size. */
    if (width > SIZE_MAX / height / (4 * sizeof(float))) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }

    jfx_ofx_plugin_slot_t *slot = instance->plugin;
    if (!slot || !slot->plugin) return JFX_ERROR_NOT_INITIALIZED;

    jfx_ofx_clip_t *output = find_clip(instance, kOfxImageEffectOutputClipName);
    jfx_ofx_clip_t *source = find_clip(instance,
        kOfxImageEffectSimpleSourceClipName);
    if (!output) return JFX_ERROR_NOT_INITIALIZED;
    /* A filter that declared a source clip needs one; a generator has none. */
    if (source && !desc->src) return JFX_ERROR_INVALID_ARGUMENT;

    size_t values = width * height * 4;
    size_t bytes = values * sizeof(float);

    /* Render into scratch so a failing or aborting plugin cannot leave the
     * caller's destination partly written, matching the transactional output
     * rule the engine's CPU image path already follows. */
    float *scratch = tilly_container_alloc(bytes);
    if (!scratch) return JFX_ERROR_OUT_OF_MEMORY;
    memcpy(scratch, desc->dst, bytes);

    output->pixels = scratch;
    output->width = width;
    output->height = height;
    output->components = 4;
    if (source) {
        source->pixels = (float *)desc->src;
        source->width = width;
        source->height = height;
        source->components = 4;
    }

    instance->aborted = 0;
    instance->error_message[0] = 0;

    jfx_ofx_property_set_t in;
    jfx_ofx_props_init(&in);
    /* OFX time counts frames. This host is frame-rate agnostic and treats the
     * caller's seconds as an opaque, monotonically increasing value; frame
     * numbers for time-dependent effects come from the timeline API. */
    jfx_ofx_props_set_double(&in, kOfxPropTime, desc->time_seconds);
    int window[4] = { 0, 0, (int)width, (int)height };
    jfx_ofx_props_set_int_array(&in, kOfxImageEffectPropRenderWindow, window, 4);
    double scale[2] = { 1.0, 1.0 };
    jfx_ofx_props_set_double_array(&in, kOfxImageEffectPropRenderScale, scale, 2);

    OfxStatus status = slot->plugin->mainEntry(kOfxImageEffectActionRender,
        instance, (OfxPropertySetHandle)&in, (OfxPropertySetHandle)&in);

    int aborted = instance->aborted;
    output->pixels = NULL;
    if (source) source->pixels = NULL;

    if (status == kOfxStatOK && !aborted) memcpy(desc->dst, scratch, bytes);
    tilly_container_free(scratch);

    if (aborted) {
        snprintf(instance->error_message, sizeof(instance->error_message),
            "OFX plugin aborted the render");
        return JFX_ERROR_BUSY;
    }
    if (status != kOfxStatOK && status != kOfxStatReplyDefault) {
        snprintf(instance->error_message, sizeof(instance->error_message),
            "OFX render failed with status %d", (int)status);
        return JFX_ERROR_PLUGIN_FAILURE;
    }
    return JFX_SUCCESS;
}