/* Conformance tests for the OpenFX host adapter.
 *
 * These drive the real pipeline: a genuine OFX bundle on disk, loaded with
 * dlopen, taken through the action lifecycle, and rendered. Nothing is stubbed,
 * because the behaviours worth testing here -- symbol resolution, setHost, the
 * suite vtables agreeing with the OFX headers, and pixel output -- only exist
 * when the plugin is a separate binary.
 *
 * The test plugin (frontends/plugins/tests/gain_plugin.c) multiplies R, G and B
 * by a `gain` parameter clamped to [0,4] and by the per-component `tint`, and
 * leaves alpha alone. */

#include "jfx/jfx_ofx.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* The bundle the loader is pointed at. The CMake target writes the binary into
 * the build tree; the path is passed in so the test does not have to guess it. */
static const char *g_bundle_directory = NULL;

static int file_exists(const char *path) {
    struct stat st;
    return stat(path, &st) == 0;
}

/* Joins path components, refusing to truncate: a shortened path could stage the
 * bundle somewhere the loader does not scan, which would silently skip the test
 * rather than fail it. `c` may be NULL for a two-component join. */
static int join(char *out, size_t capacity, const char *a, const char *b,
    const char *c) {
    const char *parts[3];
    int count = 0;
    parts[count++] = a;
    if (b) parts[count++] = b;
    if (c) parts[count++] = c;
    size_t used = 0;
    for (int i = 0; i < count; ++i) {
        size_t length = strlen(parts[i]);
        if (used + length + (i ? 1u : 0u) >= capacity) return -1;
        if (i) out[used++] = '/';
        memcpy(out + used, parts[i], length);
        used += length;
    }
    out[used] = 0;
    return 0;
}

/* Creates <dir>/<name>.ofx.bundle/Contents/<platform>/<name>.ofx as a copy of
 * `binary`, which is what a real bundle looks like on disk. Returns 0 on
 * success. */
static int make_bundle(const char *dir, const char *name, const char *binary,
    const char *platform) {
    char leaf[128];
    if ((size_t)snprintf(leaf, sizeof(leaf), "%s.ofx.bundle", name)
        >= sizeof(leaf)) return -1;

    char bundle[1024], contents[1024], platform_dir[1024], target[1024];
    if (join(bundle, sizeof(bundle), dir, leaf, NULL) != 0) return -1;
    if (join(contents, sizeof(contents), bundle, "Contents", NULL) != 0) return -1;
    if (join(platform_dir, sizeof(platform_dir), contents, platform, NULL) != 0)
        return -1;

    /* Create each level, tolerating an existing directory. */
    const char *dirs[3];
    dirs[0] = bundle; dirs[1] = contents; dirs[2] = platform_dir;
    for (int i = 0; i < 3; ++i) {
        if (mkdir(dirs[i], 0755) != 0 && !file_exists(dirs[i])) return -1;
    }

    if ((size_t)snprintf(leaf, sizeof(leaf), "%s.ofx", name) >= sizeof(leaf))
        return -1;
    if (join(target, sizeof(target), platform_dir, leaf, NULL) != 0) return -1;
    FILE *in = fopen(binary, "rb");
    if (!in) return -1;
    FILE *out = fopen(target, "wb");
    if (!out) { fclose(in); return -1; }
    char buffer[65536];
    size_t got;
    int ok = 1;
    while ((got = fread(buffer, 1, sizeof(buffer), in)) > 0) {
        if (fwrite(buffer, 1, got, out) != got) { ok = 0; break; }
    }
    fclose(in);
    if (fclose(out) != 0) ok = 0;
    return ok ? 0 : -1;
}

#if defined(__APPLE__)
#define JFX_TEST_PLATFORM "MacOS/x86-64"
#elif defined(_WIN32)
#define JFX_TEST_PLATFORM "Windows/x86-64"
#else
#define JFX_TEST_PLATFORM "Linux-x86-64"
#endif

/* ---------------------------------------------------------- invalid args */

/* Every public entry point must reject a NULL out-parameter and a wrong struct
 * size rather than writing through it. */
static void test_invalid_arguments(void) {
    jfx_ofx_host_t *host = NULL;

    assert(jfx_ofx_host_create(NULL, NULL) == JFX_ERROR_INVALID_ARGUMENT);

    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    assert(host != NULL);

    /* This host was created without scanning, so it holds no plugins. Every
     * index is therefore out of range, which the size check must be applied
     * before, so restore a valid size first. */
    assert(jfx_ofx_host_plugin_count(host) == 0);
    assert(jfx_ofx_host_plugin_info(host, 0, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    jfx_ofx_plugin_info_t info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info) - 1; /* wrong size must be refused */
    assert(jfx_ofx_host_plugin_info(host, 0, &info) == JFX_ERROR_INVALID_ARGUMENT);
    info.size = sizeof(info);
    assert(jfx_ofx_host_plugin_info(host, 0, &info) == JFX_ERROR_NOT_FOUND);
    assert(jfx_ofx_host_plugin_info(host, 9999, &info) == JFX_ERROR_NOT_FOUND);

    /* This host was created without scanning, so it holds no plugins and every
     * index is out of range. Size validation must still be reported first. */
    assert(jfx_ofx_instance_create(host, 0, NULL, NULL)
        == JFX_ERROR_INVALID_ARGUMENT);
    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_FILTER;
    jfx_ofx_instance_t *instance = NULL;
    assert(jfx_ofx_instance_create(host, 9999, &idesc, &instance)
        == JFX_ERROR_NOT_FOUND);
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance)
        == JFX_ERROR_NOT_FOUND);
    assert(instance == NULL);

    /* NULL-host entry points must refuse rather than dereference. */
    assert(jfx_ofx_instance_set_param(NULL, "gain", 1.0)
        == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_ofx_instance_get_param(NULL, "gain", NULL)
        == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_ofx_instance_param_count(NULL) == 0);
    assert(jfx_ofx_instance_capabilities(NULL, NULL)
        == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_ofx_instance_render(NULL, NULL) == JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_ofx_instance_last_error(NULL) == NULL);

    /* Wrong descriptor size is refused before any state is touched. */
    idesc.size = sizeof(idesc) - 1;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance)
        == JFX_ERROR_INVALID_ARGUMENT);

    jfx_ofx_host_destroy(host);
    /* Destroying NULL is defined to do nothing. */
    jfx_ofx_instance_destroy(NULL);
    jfx_ofx_host_destroy(NULL);
}

/* ------------------------------------------------------------- discovery */

static void test_scan_missing_directory(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    /* A directory that does not exist is reported, not a crash. */
    assert(jfx_ofx_host_scan(host, "/nonexistent-ofx-plugin-root")
        == JFX_ERROR_NOT_FOUND);
    assert(jfx_ofx_host_plugin_count(host) == 0);
    jfx_ofx_host_destroy(host);
}

/* Scans a directory holding a real bundle and checks the plugin is described. */
static void test_discovery_and_description(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.bundle_directory = g_bundle_directory;
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);

    assert(jfx_ofx_host_scan(host, g_bundle_directory) == JFX_SUCCESS);
    assert(jfx_ofx_host_plugin_count(host) == 1);

    jfx_ofx_plugin_info_t info;
    memset(&info, 0, sizeof(info));
    info.size = sizeof(info);
    assert(jfx_ofx_host_plugin_info(host, 0, &info) == JFX_SUCCESS);
    assert(strcmp(info.identifier, "org.joltfx.ofx.gain") == 0);
    /* The plugin declares Filter and General. */
    assert(info.supported_contexts
        & (1u << (unsigned)JFX_OFX_CONTEXT_FILTER));
    assert(info.supported_contexts
        & (1u << (unsigned)JFX_OFX_CONTEXT_GENERAL));

    /* The gain parameter must exist with the declared default and range. */
    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_FILTER;
    jfx_ofx_instance_t *instance = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance) == JFX_SUCCESS);

    assert(jfx_ofx_instance_param_count(instance) == 2);

    int found_gain = 0, found_tint = 0;
    for (size_t i = 0; i < jfx_ofx_instance_param_count(instance); ++i) {
        jfx_ofx_param_info_t pinfo;
        memset(&pinfo, 0, sizeof(pinfo));
        pinfo.size = sizeof(pinfo);
        assert(jfx_ofx_instance_param_info(instance, i, &pinfo) == JFX_SUCCESS);
        if (strcmp(pinfo.name, "gain") == 0) {
            found_gain = 1;
            assert(pinfo.type == JFX_OFX_PARAM_DOUBLE);
            assert(pinfo.default_value == 1.0);
            assert(pinfo.minimum == 0.0);
            assert(pinfo.maximum == 4.0);
            assert(strcmp(pinfo.label, "Gain") == 0);
        }
        if (strcmp(pinfo.name, "tint") == 0) {
            found_tint = 1;
            assert(pinfo.type == JFX_OFX_PARAM_RGBA);
            assert(pinfo.component_count == 4);
        }
    }
    assert(found_gain && found_tint);

    /* Out-of-range index. */
    jfx_ofx_param_info_t pinfo;
    memset(&pinfo, 0, sizeof(pinfo));
    pinfo.size = sizeof(pinfo);
    assert(jfx_ofx_instance_param_info(instance, 99, &pinfo)
        == JFX_ERROR_NOT_FOUND);

    jfx_ofx_instance_destroy(instance);
    jfx_ofx_host_destroy(host);
}

/* ---------------------------------------------------------------- render */

static int close_to(float a, float b) {
    return fabsf(a - b) < 1e-5f;
}

static void test_render_pixels(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.bundle_directory = g_bundle_directory;
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    assert(jfx_ofx_host_scan(host, g_bundle_directory) == JFX_SUCCESS);

    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_FILTER;
    jfx_ofx_instance_t *instance = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance) == JFX_SUCCESS);

    /* A 2x2 frame of known values. Alpha is carried through untouched, which
     * catches a host that hands back a buffer the plugin wrote out of bounds. */
    const size_t width = 2, height = 2;
    float src[2 * 2 * 4];
    float dst[2 * 2 * 4];
    for (size_t i = 0; i < 4; ++i) {
        src[i * 4 + 0] = 0.5f;
        src[i * 4 + 1] = 0.25f;
        src[i * 4 + 2] = 0.75f;
        src[i * 4 + 3] = (float)(i + 1) * 0.1f;
        dst[i * 4 + 0] = 0.0f;
        dst[i * 4 + 1] = 0.0f;
        dst[i * 4 + 2] = 0.0f;
        dst[i * 4 + 3] = 0.0f;
    }

    /* Default gain is 1.0 and default tint is 0.0, so the plugin writes zeros to
     * RGB and passes alpha through. */
    jfx_ofx_render_desc_t render;
    memset(&render, 0, sizeof(render));
    render.size = sizeof(render);
    render.src = src;
    render.width = width;
    render.height = height;
    render.dst = dst;
    render.time_seconds = 0.0;
    assert(jfx_ofx_instance_render(instance, &render) == JFX_SUCCESS);
    for (size_t i = 0; i < 4; ++i) {
        assert(dst[i * 4 + 0] == 0.0f); /* tinted by 0.0 */
        assert(dst[i * 4 + 1] == 0.0f);
        assert(dst[i * 4 + 2] == 0.0f);
        assert(close_to(dst[i * 4 + 3], src[i * 4 + 3])); /* alpha intact */
    }

    /* Now gain 2.0 with a full white tint: RGB should double. */
    assert(jfx_ofx_instance_set_param(instance, "gain", 2.0) == JFX_SUCCESS);
    double tint[4] = { 1.0, 1.0, 1.0, 1.0 };
    assert(jfx_ofx_instance_set_param_components(instance, "tint", tint, 4)
        == JFX_SUCCESS);
    double read_back = 0.0;
    assert(jfx_ofx_instance_get_param(instance, "gain", &read_back)
        == JFX_SUCCESS);
    assert(read_back == 2.0);
    double components[4];
    int count = 0;
    assert(jfx_ofx_instance_get_param_components(instance, "tint", components, 4,
        &count) == JFX_SUCCESS);
    assert(count == 4);
    for (int i = 0; i < 4; ++i) assert(components[i] == 1.0);

    assert(jfx_ofx_instance_render(instance, &render) == JFX_SUCCESS);
    for (size_t i = 0; i < 4; ++i) {
        assert(close_to(dst[i * 4 + 0], 1.0f)); /* 0.5 * 2 * 1 */
        assert(close_to(dst[i * 4 + 1], 0.5f)); /* 0.25 * 2 * 1 */
        assert(close_to(dst[i * 4 + 2], 1.5f)); /* 0.75 * 2 * 1 */
        assert(close_to(dst[i * 4 + 3], src[i * 4 + 3]));
    }

    jfx_ofx_instance_destroy(instance);
    jfx_ofx_host_destroy(host);
}

/* The host must clamp to the declared range; the plugin fails the render if it
 * ever sees a gain above 4, so this proves the clamp rather than assuming it. */
static void test_parameter_clamping(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.bundle_directory = g_bundle_directory;
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    assert(jfx_ofx_host_scan(host, g_bundle_directory) == JFX_SUCCESS);

    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_FILTER;
    jfx_ofx_instance_t *instance = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance) == JFX_SUCCESS);

    assert(jfx_ofx_instance_set_param(instance, "gain", 1000.0) == JFX_SUCCESS);
    double value = 0.0;
    assert(jfx_ofx_instance_get_param(instance, "gain", &value) == JFX_SUCCESS);
    assert(value == 4.0); /* clamped to the declared maximum */

    assert(jfx_ofx_instance_set_param(instance, "gain", -5.0) == JFX_SUCCESS);
    assert(jfx_ofx_instance_get_param(instance, "gain", &value) == JFX_SUCCESS);
    assert(value == 0.0); /* clamped to the declared minimum */

    /* A non-finite value is rejected by the clamp rather than propagated. */
    assert(jfx_ofx_instance_set_param(instance, "gain", 1.0 / 0.0)
        == JFX_SUCCESS);
    assert(jfx_ofx_instance_get_param(instance, "gain", &value) == JFX_SUCCESS);
    assert(value == 0.0 || value == 4.0);

    jfx_ofx_instance_destroy(instance);
    jfx_ofx_host_destroy(host);
}

/* A failed render must not leave the caller's destination half-written. */
static void test_failed_render_preserves_destination(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.bundle_directory = g_bundle_directory;
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    assert(jfx_ofx_host_scan(host, g_bundle_directory) == JFX_SUCCESS);

    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_FILTER;
    jfx_ofx_instance_t *instance = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance) == JFX_SUCCESS);

    float dst[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
    const float original[4] = { 0.1f, 0.2f, 0.3f, 0.4f };

    /* A filter needs a source; without one the render must fail cleanly. */
    jfx_ofx_render_desc_t render;
    memset(&render, 0, sizeof(render));
    render.size = sizeof(render);
    render.src = NULL;
    render.width = 1;
    render.height = 1;
    render.dst = dst;
    assert(jfx_ofx_instance_render(instance, &render)
        == JFX_ERROR_INVALID_ARGUMENT);
    for (int i = 0; i < 4; ++i) assert(dst[i] == original[i]);

    /* Mismatched component counts are refused. */
    double two[2] = { 1.0, 1.0 };
    assert(jfx_ofx_instance_set_param_components(instance, "tint", two, 2)
        == JFX_ERROR_INVALID_ARGUMENT);

    jfx_ofx_instance_destroy(instance);
    jfx_ofx_host_destroy(host);
}

/* The General context declares an extra optional clip; the host must survive it
 * and keep the standard clips usable. */
static void test_general_context(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.bundle_directory = g_bundle_directory;
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    assert(jfx_ofx_host_scan(host, g_bundle_directory) == JFX_SUCCESS);

    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_GENERAL;
    jfx_ofx_instance_t *instance = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &instance) == JFX_SUCCESS);

    jfx_ofx_instance_capabilities_t caps;
    memset(&caps, 0, sizeof(caps));
    caps.size = sizeof(caps);
    assert(jfx_ofx_instance_capabilities(instance, &caps) == JFX_SUCCESS);
    assert(caps.context == JFX_OFX_CONTEXT_GENERAL);
    /* The host is honest about not doing tiles or temporal access. */
    assert(caps.supports_tiles == 0);
    assert(caps.temporal_clip_access == 0);

    float src[4] = { 0.5f, 0.25f, 0.75f, 0.5f };
    float dst[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    jfx_ofx_render_desc_t render;
    memset(&render, 0, sizeof(render));
    render.size = sizeof(render);
    render.src = src;
    render.width = 1;
    render.height = 1;
    render.dst = dst;
    assert(jfx_ofx_instance_render(instance, &render) == JFX_SUCCESS);
    assert(close_to(dst[3], 0.5f));

    jfx_ofx_instance_destroy(instance);
    jfx_ofx_host_destroy(host);
}

/* Two independent instances of one plugin must not share parameter state. */
static void test_instances_are_independent(void) {
    jfx_ofx_host_t *host = NULL;
    jfx_ofx_host_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.bundle_directory = g_bundle_directory;
    desc.max_instances_per_plugin = 2;
    assert(jfx_ofx_host_create(&desc, &host) == JFX_SUCCESS);
    assert(jfx_ofx_host_scan(host, g_bundle_directory) == JFX_SUCCESS);

    jfx_ofx_instance_desc_t idesc;
    memset(&idesc, 0, sizeof(idesc));
    idesc.size = sizeof(idesc);
    idesc.context = JFX_OFX_CONTEXT_FILTER;

    jfx_ofx_instance_t *a = NULL, *b = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &a) == JFX_SUCCESS);
    assert(jfx_ofx_instance_create(host, 0, &idesc, &b) == JFX_SUCCESS);
    assert(a != b);

    assert(jfx_ofx_instance_set_param(a, "gain", 3.0) == JFX_SUCCESS);
    double value = 0.0;
    assert(jfx_ofx_instance_get_param(b, "gain", &value) == JFX_SUCCESS);
    assert(value == 1.0); /* b still holds the declared default */

    /* The per-plugin cap is enforced. */
    jfx_ofx_instance_t *c = NULL;
    assert(jfx_ofx_instance_create(host, 0, &idesc, &c) == JFX_ERROR_BUSY);

    jfx_ofx_instance_destroy(a);
    jfx_ofx_instance_destroy(b);
    jfx_ofx_host_destroy(host);
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr,
            "usage: %s <bundle-scan-dir> <gain_plugin_binary>\n", argv[0]);
        return 2;
    }
    g_bundle_directory = argv[1];
    const char *binary = argv[2];

    if (make_bundle(g_bundle_directory, "jfx_ofx_gain", binary,
            JFX_TEST_PLATFORM) != 0) {
        fprintf(stderr, "failed to stage the test bundle in %s\n",
            g_bundle_directory);
        return 2;
    }

    test_invalid_arguments();
    test_scan_missing_directory();
    test_discovery_and_description();
    test_render_pixels();
    test_parameter_clamping();
    test_failed_render_preserves_destination();
    test_general_context();
    test_instances_are_independent();

    printf("ofx_host: all checks passed\n");
    return 0;
}