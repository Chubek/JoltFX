#include "vk_compute.h"

#include <dlfcn.h>
#include <errno.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "joltscript/vm.h"
#include "tilly/allocator.h"
#include "tilly/logger.h"
#include "vk_minimal.h"

#if defined(__unix__) || defined(__APPLE__)
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#define JVK_HAVE_POSIX_SPAWN 1
#endif

#define JVK_WORKGROUP 256u
#define JVK_FENCE_TIMEOUT_NS ((uint64_t)10000000000u)
#define JVK_CODEGEN_CAP ((size_t)8u * 1024u * 1024u)

struct jvk_compute_device {
    void *loader;
    jvk_fns_t fns;
    jvk_instance_t instance;
    jvk_physical_device_t physical;
    jvk_device_t device;
    jvk_queue_t queue;
    uint32_t queue_family;
    jvk_command_pool_t pool;
};

typedef struct {
    uint32_t flags;
    uint32_t heap_index;
} jvk_memory_type_t;

typedef struct {
    uint64_t size;
    uint32_t flags;
} jvk_memory_heap_t;

typedef struct {
    uint32_t type_count;
    uint32_t padding0;
    jvk_memory_type_t types[32];
    uint32_t heap_count;
    uint32_t padding1;
    jvk_memory_heap_t heaps[16];
} jvk_memory_properties_t;

static void *vk_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static bool debug_enabled(void) {
    static int cached = -1;
    if (cached < 0) {
        cached = getenv("JFX_VK_DEBUG") ? 1 : 0;
    }
    return cached > 0;
}

static void debug_stage(const char *stage, long result) {
    if (debug_enabled()) {
        fprintf(stderr, "[jfx-vk] %s: result=%ld\n", stage, result);
    }
}
static void vk_free(void *ptr) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), ptr);
}

static uint32_t read_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
        ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

jvk_compute_device_t *jvk_device_create(char *name, size_t name_size,
    uint32_t *api_version) {
    if (name && name_size) {
        snprintf(name, name_size, "cpu-fallback");
    }
    if (api_version) {
        *api_version = 0;
    }
    void *loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!loader) {
        loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    jvk_compute_device_t *dev = vk_alloc(sizeof(*dev));
    if (!dev) {
        if (loader) {
            dlclose(loader);
        }
        return NULL;
    }
    memset(dev, 0, sizeof(*dev));
    dev->loader = loader;
    if (!loader || !jvk_load_fns(loader, &dev->fns)) {
        debug_stage("load", loader ? -100 : -101);
        goto fail;
    }
    debug_stage("load", JVK_SUCCESS);
    if (dev->fns.enumerate_instance_version) {
        uint32_t v = 0;
        jvk_result_t queried = dev->fns.enumerate_instance_version(&v);
        debug_stage("instance-version", queried);
        if (queried == JVK_SUCCESS && api_version) {
            *api_version = v;
        }
    }
    {
        jvk_application_info_t app = {
            .s_type = JVK_STRUCTURE_TYPE_APPLICATION_INFO,
            .p_next = NULL,
            .p_application_name = "joltfx",
            .application_version = (0u << 22) | (2u << 12),
            .p_engine_name = "joltfx",
            .engine_version = (0u << 22) | (2u << 12),
            .api_version = JVK_API_VERSION_1_0,
        };
        jvk_instance_create_info_t info = {
            .s_type = JVK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .p_application_info = &app,
            .enabled_layer_count = 0,
            .pp_enabled_layer_names = NULL,
            .enabled_extension_count = 0,
            .pp_enabled_extension_names = NULL,
        };
        jvk_result_t created =
            dev->fns.create_instance(&info, NULL, &dev->instance);
        debug_stage("create-instance", created);
        if (created != JVK_SUCCESS || !dev->instance) {
            goto fail;
        }
    }
    {
        uint32_t count = 0;
        if (dev->fns.enumerate_physical_devices(dev->instance, &count, NULL) !=
                JVK_SUCCESS ||
            !count) {
            goto fail;
        }
        jvk_physical_device_t *list = vk_alloc(count * sizeof(*list));
        if (!list) {
            goto fail;
        }
        jvk_result_t status =
            dev->fns.enumerate_physical_devices(dev->instance, &count, list);
        jvk_physical_device_t pick = NULL;
        unsigned char *scratch = vk_alloc(JVK_PHYSICAL_DEVICE_PROPERTIES_SCRATCH);
        if (status == JVK_SUCCESS && scratch) {
            for (uint32_t i = 0; i < count && status == JVK_SUCCESS; ++i) {
                memset(scratch, 0, JVK_PHYSICAL_DEVICE_PROPERTIES_SCRATCH);
                dev->fns.get_physical_device_properties(list[i], scratch);
                int32_t type = 0;
                memcpy(&type, scratch + JVK_DEVICE_TYPE_OFFSET, sizeof(type));
                if (!pick || type == JVK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    pick = list[i];
                    if (name && name_size) {
                        snprintf(name, name_size, "%s",
                            (const char *)(scratch + JVK_DEVICE_NAME_OFFSET));
                        name[name_size - 1] = 0;
                    }
                }
                if (type == JVK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                    break;
                }
            }
        }
        vk_free(scratch);
        vk_free(list);
        if (!pick) {
            goto fail;
        }
        dev->physical = pick;
    }
    {
        uint32_t count = 0;
        dev->fns.get_physical_device_queue_family_properties(dev->physical,
            &count, NULL);
        if (!count) {
            goto fail;
        }
        jvk_queue_family_properties_t *families =
            vk_alloc(count * sizeof(*families));
        if (!families) {
            goto fail;
        }
        dev->fns.get_physical_device_queue_family_properties(dev->physical,
            &count, families);
        uint32_t family = count;
        for (uint32_t i = 0; i < count; ++i) {
            if (families[i].queue_flags & JVK_QUEUE_COMPUTE_BIT) {
                family = i;
                break;
            }
        }
        vk_free(families);
        if (family == count) {
            goto fail;
        }
        dev->queue_family = family;
        float priority = 1.0f;
        jvk_device_queue_create_info_t queue = {
            .s_type = JVK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .queue_family_index = family,
            .queue_count = 1,
            .p_queue_priorities = &priority,
        };
        jvk_device_create_info_t info = {
            .s_type = JVK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .queue_create_info_count = 1,
            .p_queue_create_infos = &queue,
            .enabled_layer_count = 0,
            .pp_enabled_layer_names = NULL,
            .enabled_extension_count = 0,
            .pp_enabled_extension_names = NULL,
            .p_enabled_features = NULL,
        };
        if (dev->fns.create_device(dev->physical, &info, NULL, &dev->device) !=
                JVK_SUCCESS ||
            !dev->device) {
            goto fail;
        }
        dev->fns.get_device_queue(dev->device, family, 0, &dev->queue);
        if (!dev->queue) {
            goto fail;
        }
        jvk_command_pool_create_info_t pool = {
            .s_type = JVK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .queue_family_index = family,
        };
        if (dev->fns.create_command_pool(dev->device, &pool, NULL, &dev->pool) !=
                JVK_SUCCESS ||
            !dev->pool) {
            goto fail;
        }
    }
    return dev;
fail:
    jvk_device_destroy(dev);
    return NULL;
}

void jvk_device_destroy(jvk_compute_device_t *dev) {
    if (!dev) {
        return;
    }
    if (dev->device) {
        dev->fns.device_wait_idle(dev->device);
        if (dev->pool) {
            dev->fns.destroy_command_pool(dev->device, dev->pool, NULL);
        }
        dev->fns.destroy_device(dev->device, NULL);
    }
    if (dev->instance) {
        dev->fns.destroy_instance(dev->instance, NULL);
    }
    if (dev->loader) {
        dlclose(dev->loader);
    }
    vk_free(dev);
}

/* ---------------- string builder ---------------- */

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    bool failed;
} builder_t;

static void emit(builder_t *b, const char *text) {
    if (b->failed) {
        return;
    }
    size_t n = strlen(text);
    if (b->length > JVK_CODEGEN_CAP || n > JVK_CODEGEN_CAP - b->length ||
        b->length + n + 1 < b->length) {
        b->failed = true;
        return;
    }
    if (b->length + n + 1 > b->capacity) {
        size_t next = b->capacity ? b->capacity : 1024;
        while (next < b->length + n + 1) {
            next *= 2;
        }
        char *grown = tilly_realloc((tilly_allocator_t *)tilly_default_allocator(),
            b->data, next);
        if (!grown) {
            b->failed = true;
            return;
        }
        b->data = grown;
        b->capacity = next;
    }
    memcpy(b->data + b->length, text, n + 1);
    b->length += n;
}

/* ---------------- JBC1 -> GLSL ---------------- */

static const char *component_name(uint32_t input) {
    switch (input) {
    case 0:
        return "px.x";
    case 1:
        return "px.y";
    case 2:
        return "px.z";
    default:
        return "px.w";
    }
}

/* Appends a compute shader implementing validated bytecode. Outputs must be
 * 4 (RGBA); inputs are 4 pixel components plus param_count uniforms. */
static void codegen(const uint8_t *code, size_t size, size_t param_count,
    builder_t *b) {
    char line[96];
    emit(b,
        "#version 450\n"
        "layout(local_size_x = 256) in;\n"
        "layout(set = 0, binding = 0) readonly buffer InBuf { vec4 v[]; } vin;\n"
        "layout(set = 0, binding = 1) writeonly buffer OutBuf { vec4 v[]; } vout;\n"
        "layout(push_constant) uniform Push { uint pixel_count;");
    if (param_count) {
        snprintf(line, sizeof(line), " float params[%u];", (unsigned)param_count);
        emit(b, line);
    }
    emit(b,
        " } pc;\n"
        "void main() {\n"
        " uint idx = gl_GlobalInvocationID.x;\n"
        " if (idx >= pc.pixel_count) return;\n"
        " vec4 px = vin.v[idx];\n"
        " float o[4];\n");
    unsigned temps[JOLT_MAX_STACK];
    unsigned depth = 0, seq = 0;
    for (size_t pos = 16; pos + 8 <= size && pos + 8 >= pos; pos += 8) {
        uint32_t op = read_u32(code + pos), arg = read_u32(code + pos + 4);
        switch (op) {
        case JOLT_OP_CONST:
            snprintf(line, sizeof(line), " float t%u = uintBitsToFloat(0x%08xu);\n",
                seq, arg);
            emit(b, line);
            temps[depth++] = seq++;
            break;
        case JOLT_OP_INPUT:
            if (arg < 4) {
                snprintf(line, sizeof(line), " float t%u = %s;\n", seq,
                    component_name(arg));
            } else {
                snprintf(line, sizeof(line), " float t%u = pc.params[%u];\n", seq,
                    arg - 4);
            }
            emit(b, line);
            temps[depth++] = seq++;
            break;
        case JOLT_OP_OUTPUT:
            snprintf(line, sizeof(line), " o[%u] = t%u;\n", arg, temps[--depth]);
            emit(b, line);
            break;
        case JOLT_OP_SELECT: {
            unsigned c = temps[depth - 3], a = temps[depth - 2], x = temps[depth - 1];
            depth -= 2;
            temps[depth - 1] = seq;
            snprintf(line, sizeof(line),
                " float t%u = ((t%u != 0.0) ? t%u : t%u);\n", seq, c, a, x);
            emit(b, line);
            ++seq;
            break;
        }
        case JOLT_OP_LT: {
            unsigned x = temps[--depth], a = temps[--depth];
            snprintf(line, sizeof(line), " float t%u = ((t%u < t%u) ? 1.0 : 0.0);\n",
                seq, a, x);
            emit(b, line);
            temps[depth++] = seq++;
            break;
        }
        default: {
            const char *name = NULL;
            char infix = 0;
            bool unary = false;
            switch (op) {
            case JOLT_OP_ADD: infix = '+'; break;
            case JOLT_OP_SUB: infix = '-'; break;
            case JOLT_OP_MUL: infix = '*'; break;
            case JOLT_OP_DIV: infix = '/'; break;
            case JOLT_OP_MIN: name = "min"; break;
            case JOLT_OP_MAX: name = "max"; break;
            case JOLT_OP_ABS: name = "abs"; unary = true; break;
            case JOLT_OP_FLOOR: name = "floor"; unary = true; break;
            case JOLT_OP_POW: name = "pow"; break;
            case JOLT_OP_SQRT: name = "sqrt"; unary = true; break;
            default: b->failed = true; return;
            }
            if (name && unary) {
                unsigned a = temps[--depth];
                snprintf(line, sizeof(line), " float t%u = %s(t%u);\n", seq,
                    name, a);
                emit(b, line);
                temps[depth++] = seq++;
            } else if (name) {
                unsigned x = temps[--depth], a = temps[--depth];
                snprintf(line, sizeof(line), " float t%u = %s(t%u, t%u);\n", seq,
                    name, a, x);
                emit(b, line);
                temps[depth++] = seq++;
            } else {
                unsigned x = temps[--depth], a = temps[--depth];
                snprintf(line, sizeof(line), " float t%u = (t%u %c t%u);\n", seq,
                    a, infix, x);
                emit(b, line);
                temps[depth++] = seq++;
            }
            break;
        }
        }
    }
    emit(b, " vout.v[idx] = vec4(o[0], o[1], o[2], o[3]);\n}\n");
}

/* ---------------- SPIR-V via glslc ---------------- */

static uint64_t fnv1a(const char *data, size_t size) {
    uint64_t hash = 14695981039346656037u;
    for (size_t i = 0; i < size; ++i) {
        hash ^= (uint64_t)(unsigned char)data[i];
        hash *= 1099511628211u;
    }
    return hash;
}

#if JVK_HAVE_POSIX_SPAWN
static void mkdir_p(const char *path) {
    char scratch[4096];
    snprintf(scratch, sizeof(scratch), "%s", path);
    for (char *s = scratch + 1; *s; ++s) {
        if (*s == '/') {
            *s = 0;
            mkdir(scratch, 0700);
            *s = '/';
        }
    }
    mkdir(scratch, 0700);
}

/* Builds the on-disk cache paths for a shader, keyed by its content hash.
 *
 * Returns false, writing neither buffer, when the cache root would not fit: a
 * truncated path would name a different file than intended, and a silently
 * shortened cache key is worse than no cache at all. */
static bool cache_paths(char *comp, size_t comp_size, char *spv,
    size_t spv_size, uint64_t hash) {
    char dir[4096];
    const char *base = getenv("XDG_CACHE_HOME");
    if (base && base[0]) {
        snprintf(dir, sizeof(dir), "%s/joltfx", base);
    } else if ((base = getenv("HOME")) && base[0]) {
        snprintf(dir, sizeof(dir), "%s/.cache/joltfx", base);
    } else {
        snprintf(dir, sizeof(dir), "/tmp/joltfx-%d", (int)getuid());
    }
    const int dir_length = snprintf(NULL, 0, "%s", dir);
    if (dir_length < 0 || (size_t)dir_length >= sizeof(dir)) {
        return false;
    }
    /* separator + "jfx-" + 16 hex digits + ".comp" + NUL (".spv" is shorter) */
    const size_t needed = (size_t)dir_length + 1u + 4u + 16u + 5u + 1u;
    if (needed >= comp_size || needed >= spv_size) {
        return false;
    }
    mkdir_p(dir);
    snprintf(comp, comp_size, "%s/jfx-%016llx.comp", dir, (unsigned long long)hash);
    snprintf(spv, spv_size, "%s/jfx-%016llx.spv", dir, (unsigned long long)hash);
    return true;
}

static bool run_glslc(const char *comp, const char *spv) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid == 0) {
        execlp("glslc", "glslc", "-fshader-stage=compute",
            "--target-env=vulkan1.0", "-O", "-o", spv, comp, (char *)NULL);
        _exit(127);
    }
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

static uint32_t *load_spv(const char *glsl, size_t *out_size) {
    *out_size = 0;
#if !JVK_HAVE_POSIX_SPAWN
    (void)glsl;
    tilly_log_simple(TILLY_LOG_ERROR, "Vulkan backend requires POSIX process support");
    return NULL;
#else
    char comp[4096] = { 0 }, spv[4096] = { 0 };
    if (!cache_paths(comp, sizeof(comp), spv, sizeof(spv), fnv1a(glsl, strlen(glsl)))) {
        tilly_log_simple(TILLY_LOG_ERROR,
            "Vulkan shader cache path does not fit; set XDG_CACHE_HOME or TMPDIR to a shorter path");
        return NULL;
    }
    FILE *hit = fopen(spv, "rb");
    if (!hit) {
        FILE *src = fopen(comp, "wb");
        if (!src) {
            return NULL;
        }
        size_t length = strlen(glsl);
        bool ok = fwrite(glsl, 1, length, src) == length && !fclose(src);
        if (!ok) {
            return NULL;
        }
        if (!run_glslc(comp, spv)) {
            tilly_log_simple(TILLY_LOG_ERROR, "Vulkan backend shader compile failed");
            return NULL;
        }
        hit = fopen(spv, "rb");
        if (!hit) {
            return NULL;
        }
    }
    fseek(hit, 0, SEEK_END);
    long bytes = ftell(hit);
    fseek(hit, 0, SEEK_SET);
    if (bytes <= 0 || (bytes % 4) != 0 || (size_t)bytes > 16 * 1024 * 1024) {
        fclose(hit);
        return NULL;
    }
    uint32_t *words = vk_alloc((size_t)bytes);
    bool ok = words && fread(words, 1, (size_t)bytes, hit) == (size_t)bytes;
    fclose(hit);
    if (!ok) {
        vk_free(words);
        return NULL;
    }
    *out_size = (size_t)bytes;
    return words;
#endif
}

/* ---------------- device buffers ---------------- */

typedef struct {
    jvk_buffer_t buffer;
    jvk_device_memory_t memory;
    void *mapped;
    bool coherent;
    jvk_device_size_t size;
} jvk_image_buf_t;

static bool image_buf_create(jvk_compute_device_t *dev, size_t bytes,
    jvk_image_buf_t *out) {
    memset(out, 0, sizeof(*out));
    const jvk_fns_t *f = &dev->fns;
    jvk_buffer_create_info_t info = {
        .s_type = JVK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .size = (jvk_device_size_t)bytes,
        .usage = JVK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharing_mode = JVK_SHARING_MODE_EXCLUSIVE,
        .queue_family_index_count = 0,
        .p_queue_family_indices = NULL,
    };
    if (f->create_buffer(dev->device, &info, NULL, &out->buffer) != JVK_SUCCESS ||
        !out->buffer) {
        return false;
    }
    jvk_memory_requirements_t req = {0};
    f->get_buffer_memory_requirements(dev->device, out->buffer, &req);
    out->size = req.size;
    unsigned char props[sizeof(jvk_memory_properties_t)];
    memset(props, 0, sizeof(props));
    f->get_physical_device_memory_properties(dev->physical, props);
    const jvk_memory_properties_t *memory = (const void *)props;
    uint32_t picked = memory->type_count;
    for (uint32_t i = 0; i < memory->type_count; ++i) {
        if (!(req.memory_type_bits & (1u << i))) {
            continue;
        }
        if (!(memory->types[i].flags & JVK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
            continue;
        }
        picked = i;
        if (memory->types[i].flags & JVK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
            break;
        }
    }
    if (picked == memory->type_count) {
        return false;
    }
    out->coherent =
        (memory->types[picked].flags & JVK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0;
    jvk_memory_allocate_info_t alloc = {
        .s_type = JVK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .p_next = NULL,
        .allocation_size = req.size,
        .memory_type_index = picked,
    };
    if (f->allocate_memory(dev->device, &alloc, NULL, &out->memory) != JVK_SUCCESS ||
        !out->memory ||
        f->bind_buffer_memory(dev->device, out->buffer, out->memory, 0) !=
            JVK_SUCCESS ||
        f->map_memory(dev->device, out->memory, 0, JVK_WHOLE_SIZE, 0,
            &out->mapped) != JVK_SUCCESS ||
        !out->mapped) {
        return false;
    }
    return true;
}

static void image_buf_flush(jvk_compute_device_t *dev, const jvk_image_buf_t *buf) {
    if (!buf->coherent) {
        jvk_mapped_memory_range_t range = {
            .s_type = JVK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .p_next = NULL,
            .memory = buf->memory,
            .offset = 0,
            .size = JVK_WHOLE_SIZE,
        };
        dev->fns.flush_mapped_memory_ranges(dev->device, 1, &range);
    }
}

static void image_buf_invalidate(jvk_compute_device_t *dev, const jvk_image_buf_t *buf) {
    if (!buf->coherent) {
        jvk_mapped_memory_range_t range = {
            .s_type = JVK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .p_next = NULL,
            .memory = buf->memory,
            .offset = 0,
            .size = JVK_WHOLE_SIZE,
        };
        dev->fns.invalidate_mapped_memory_ranges(dev->device, 1, &range);
    }
}

static void image_buf_destroy(jvk_compute_device_t *dev, jvk_image_buf_t *buf) {
    if (buf->mapped) {
        dev->fns.unmap_memory(dev->device, buf->memory);
    }
    if (buf->memory) {
        dev->fns.free_memory(dev->device, buf->memory, NULL);
    }
    if (buf->buffer) {
        dev->fns.destroy_buffer(dev->device, buf->buffer, NULL);
    }
    memset(buf, 0, sizeof(*buf));
}

/* ---------------- dispatch ---------------- */

int jvk_compute_run(jvk_compute_device_t *dev, const uint8_t *code, size_t size,
    const float *input_rgba, size_t pixels, const float *parameters,
    size_t parameter_count, float *output_rgba, size_t memory_limit) {
    if (!dev || !code || !input_rgba || !output_rgba || !pixels ||
        pixels > SIZE_MAX / (4 * sizeof(float)) ||
        parameter_count > JOLT_MAX_INPUTS - 4 ||
        (parameter_count && !parameters)) {
        return -1;
    }
    if (jolt_bytecode_validate(code, size) != JOLT_OK ||
        read_u32(code + 8) != 4 + (uint32_t)parameter_count ||
        read_u32(code + 12) != 4) {
        return -1;
    }
    size_t frame = pixels * 4 * sizeof(float);
    if (frame > memory_limit / 2) {
        return -3;
    }
    size_t count = pixels * 4;
    for (size_t i = 0; i < count; ++i) {
        if (!isfinite(input_rgba[i])) {
            return -2;
        }
    }
    for (size_t i = 0; i < parameter_count; ++i) {
        if (!isfinite(parameters[i])) {
            return -2;
        }
    }
    const jvk_fns_t *f = &dev->fns;
    int status = -1;
    builder_t b = {0};
    codegen(code, size, parameter_count, &b);
    if (b.failed || !b.data) {
        vk_free(b.data);
        return -1;
    }
    size_t spv_size = 0;
    uint32_t *spv = load_spv(b.data, &spv_size);
    vk_free(b.data);
    if (!spv) {
        return -1;
    }

    jvk_image_buf_t in = {0}, out = {0};
    jvk_descriptor_set_layout_t set_layout = 0;
    jvk_pipeline_layout_t pipe_layout = 0;
    jvk_descriptor_pool_t pool = 0;
    jvk_shader_module_t module = 0;
    jvk_pipeline_t pipeline = 0;
    jvk_command_buffer_t cmd = NULL;
    jvk_fence_t fence = 0;

    uint32_t push_size = (uint32_t)(sizeof(uint32_t) + parameter_count * sizeof(float));
    uint8_t push[sizeof(uint32_t) + (JOLT_MAX_INPUTS - 4) * sizeof(float)];
    /* JBC1 pixel counts always fit the address space here: pixels is bounded
     * above by memory_limit/32, far below 2^32 on 64-bit. */
    if ((uint64_t)pixels > (uint64_t)UINT32_MAX) {
        goto done;
    }
    {
        uint32_t count32 = (uint32_t)pixels;
        memcpy(push, &count32, sizeof(count32));
    }
    if (parameter_count) {
        memcpy(push + sizeof(uint32_t), parameters, parameter_count * sizeof(float));
    }

    if (!image_buf_create(dev, frame, &in) || !image_buf_create(dev, frame, &out)) {
        goto done;
    }
    memcpy(in.mapped, input_rgba, frame);
    image_buf_flush(dev, &in);

    jvk_descriptor_set_layout_binding_t bindings[2] = {
        {0, JVK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, JVK_SHADER_STAGE_COMPUTE_BIT,
            NULL},
        {1, JVK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, JVK_SHADER_STAGE_COMPUTE_BIT,
            NULL},
    };
    jvk_descriptor_set_layout_create_info_t set_info = {
        .s_type = JVK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .binding_count = 2,
        .p_bindings = bindings,
    };
    if (f->create_descriptor_set_layout(dev->device, &set_info, NULL,
            &set_layout) != JVK_SUCCESS ||
        !set_layout) {
        goto done;
    }
    jvk_push_constant_range_t range = {
        .stage_flags = JVK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = push_size,
    };
    jvk_pipeline_layout_create_info_t layout_info = {
        .s_type = JVK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .set_layout_count = 1,
        .p_set_layouts = &set_layout,
        .push_constant_range_count = 1,
        .p_push_constant_ranges = &range,
    };
    if (f->create_pipeline_layout(dev->device, &layout_info, NULL,
            &pipe_layout) != JVK_SUCCESS ||
        !pipe_layout) {
        goto done;
    }
    jvk_descriptor_pool_size_t pool_size = {JVK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2};
    jvk_descriptor_pool_create_info_t pool_info = {
        .s_type = JVK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .max_sets = 1,
        .pool_size_count = 1,
        .p_pool_sizes = &pool_size,
    };
    jvk_descriptor_set_t set = 0;
    if (f->create_descriptor_pool(dev->device, &pool_info, NULL, &pool) !=
            JVK_SUCCESS ||
        !pool) {
        goto done;
    }
    jvk_descriptor_set_allocate_info_t alloc_info = {
        .s_type = JVK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .p_next = NULL,
        .descriptor_pool = pool,
        .descriptor_set_count = 1,
        .p_set_layouts = &set_layout,
    };
    if (f->allocate_descriptor_sets(dev->device, &alloc_info, &set) != JVK_SUCCESS ||
        !set) {
        goto done;
    }
    jvk_descriptor_buffer_info_t buffers[2] = {
        {in.buffer, 0, JVK_WHOLE_SIZE},
        {out.buffer, 0, JVK_WHOLE_SIZE},
    };
    jvk_write_descriptor_set_t writes[2] = {
        {JVK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 0, 0, 1,
            JVK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &buffers[0], NULL},
        {JVK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, NULL, set, 1, 0, 1,
            JVK_DESCRIPTOR_TYPE_STORAGE_BUFFER, NULL, &buffers[1], NULL},
    };
    f->update_descriptor_sets(dev->device, 2, writes, 0, NULL);

    jvk_shader_module_create_info_t module_info = {
        .s_type = JVK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .code_size = spv_size,
        .p_code = spv,
    };
    if (f->create_shader_module(dev->device, &module_info, NULL, &module) !=
            JVK_SUCCESS ||
        !module) {
        goto done;
    }
    jvk_compute_pipeline_create_info_t pipe_info = {
        .s_type = JVK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .stage =
            {
                .s_type = JVK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .p_next = NULL,
                .flags = 0,
                .stage = JVK_SHADER_STAGE_COMPUTE_BIT,
                .module = module,
                .p_name = "main",
                .p_specialization_info = NULL,
            },
        .layout = pipe_layout,
        .base_pipeline_handle = 0,
        .base_pipeline_index = -1,
    };
    if (f->create_compute_pipelines(dev->device, 0, 1, &pipe_info, NULL,
            &pipeline) != JVK_SUCCESS ||
        !pipeline) {
        goto done;
    }
    jvk_command_buffer_allocate_info_t cmd_info = {
        .s_type = JVK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .p_next = NULL,
        .command_pool = dev->pool,
        .level = JVK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .command_buffer_count = 1,
    };
    if (f->allocate_command_buffers(dev->device, &cmd_info, &cmd) != JVK_SUCCESS ||
        !cmd) {
        goto done;
    }
    jvk_command_buffer_begin_info_t begin = {
        .s_type = JVK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .p_next = NULL,
        .flags = JVK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        .p_inheritance_info = NULL,
    };
    jvk_memory_barrier_t acquire = {
        .s_type = JVK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .p_next = NULL,
        .src_access_mask = JVK_ACCESS_HOST_WRITE_BIT,
        .dst_access_mask = JVK_ACCESS_SHADER_READ_BIT,
    };
    jvk_memory_barrier_t release = {
        .s_type = JVK_STRUCTURE_TYPE_MEMORY_BARRIER,
        .p_next = NULL,
        .src_access_mask = JVK_ACCESS_SHADER_WRITE_BIT,
        .dst_access_mask = JVK_ACCESS_HOST_READ_BIT,
    };
    uint32_t groups = (uint32_t)(((uint64_t)pixels + JVK_WORKGROUP - 1) / JVK_WORKGROUP);
    if (f->begin_command_buffer(cmd, &begin) != JVK_SUCCESS) {
        goto done;
    }
    f->cmd_pipeline_barrier(cmd, JVK_PIPELINE_STAGE_HOST_BIT,
        JVK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1, &acquire, 0, NULL, 0, NULL);
    f->cmd_bind_pipeline(cmd, JVK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    f->cmd_bind_descriptor_sets(cmd, JVK_PIPELINE_BIND_POINT_COMPUTE, pipe_layout,
        0, 1, &set, 0, NULL);
    f->cmd_push_constants(cmd, pipe_layout, JVK_SHADER_STAGE_COMPUTE_BIT, 0,
        push_size, push);
    f->cmd_dispatch(cmd, groups, 1, 1);
    f->cmd_pipeline_barrier(cmd, JVK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        JVK_PIPELINE_STAGE_HOST_BIT, 0, 1, &release, 0, NULL, 0, NULL);
    if (f->end_command_buffer(cmd) != JVK_SUCCESS) {
        goto done;
    }
    jvk_fence_create_info_t fence_info = {
        .s_type = JVK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
    };
    if (f->create_fence(dev->device, &fence_info, NULL, &fence) != JVK_SUCCESS ||
        !fence) {
        goto done;
    }
    jvk_submit_info_t submit = {
        .s_type = JVK_STRUCTURE_TYPE_SUBMIT_INFO,
        .p_next = NULL,
        .wait_semaphore_count = 0,
        .p_wait_semaphores = NULL,
        .p_wait_dst_stage_mask = NULL,
        .command_buffer_count = 1,
        .p_command_buffers = &cmd,
        .signal_semaphore_count = 0,
        .p_signal_semaphores = NULL,
    };
    if (f->queue_submit(dev->queue, 1, &submit, fence) != JVK_SUCCESS ||
        f->wait_for_fences(dev->device, 1, &fence, 1, JVK_FENCE_TIMEOUT_NS) !=
            JVK_SUCCESS) {
        goto done;
    }
    image_buf_invalidate(dev, &out);
    {
        const float *results = (const float *)out.mapped;
        for (size_t i = 0; i < count; ++i) {
            if (!isfinite(results[i])) {
                status = -2;
                goto done;
            }
        }
        memcpy(output_rgba, results, frame);
    }
    status = 0;
done:
    if (fence) {
        f->destroy_fence(dev->device, fence, NULL);
    }
    if (cmd) {
        f->free_command_buffers(dev->device, dev->pool, 1, &cmd);
    }
    if (pipeline) {
        f->destroy_pipeline(dev->device, pipeline, NULL);
    }
    if (module) {
        f->destroy_shader_module(dev->device, module, NULL);
    }
    if (pool) {
        f->destroy_descriptor_pool(dev->device, pool, NULL);
    }
    if (set_layout) {
        f->destroy_descriptor_set_layout(dev->device, set_layout, NULL);
    }
    if (pipe_layout) {
        f->destroy_pipeline_layout(dev->device, pipe_layout, NULL);
    }
    image_buf_destroy(dev, &in);
    image_buf_destroy(dev, &out);
    vk_free(spv);
    return status;
}
