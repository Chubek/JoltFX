/* Offscreen Vulkan mesh renderer for the 3D scene viewport.
 *
 * Renders object-space triangle soup with per-draw model/normal matrices
 * through real vertex/fragment shaders (vk_scene.vert/frag, embedded as
 * SPIR-V in vk_scene_shaders.h) into an offscreen image, then reads back
 * RGBA8. No swapchain, no surface, no SDK at build time: the graphics
 * subset in vk_minimal.h is resolved with dlsym like the compute path.
 *
 * Every failure fails closed (false) and the caller falls back to the CPU
 * rasterizer: no device, no graphics queue, unsupported color/depth
 * formats, oversized scenes, or any non-SUCCESS VkResult. Output is
 * untouched on failure. All resources are transient per call. */
#include "vk_minimal.h"
#include "vk_scene_shaders.h"

#include "jfx/vk_backend.h"

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tilly/allocator.h"

#define SCENE_FENCE_TIMEOUT_NS ((uint64_t)10000000000u)
#define SCENE_MAX_DRAWS 4096u
#define SCENE_MAX_VERTS ((uint32_t)1048576u)
#define SCENE_UBO_SIZE 224u
#define SCENE_VERT_STRIDE 24u

/* Draw description (mirrored in vk_backend.h for jfx_core callers). */

typedef struct {
    uint32_t property_flags;
    uint32_t heap_index;
} scene_mem_type_t;

typedef struct {
    uint32_t type_count;
    scene_mem_type_t types[32];
    uint32_t heap_count;
    /* Heap entries are never read (only type_count/types[] are); reserve
     * the real trailing footprint so the driver write cannot overrun. */
    unsigned char heap_reserved[260];
} scene_mem_props_t;

static void *scene_alloc(size_t bytes) {
    return tilly_alloc((tilly_allocator_t *)tilly_default_allocator(), bytes,
        _Alignof(max_align_t));
}

static bool scene_debug(void) {
    static int cached = -1;
    if (cached < 0) {
        const char *flag = getenv("JFX_VK_SCENE_DEBUG");
        cached = (flag && flag[0]) ? 1 : 0;
    }
    return cached > 0;
}

static void scene_free(void *ptr) {
    tilly_free((tilly_allocator_t *)tilly_default_allocator(), ptr);
}

static bool scene_features(uint32_t props, uint32_t want) {
    return (props & want) == want;
}

/* Pick a memory type satisfying `bits`; prefer HOST_VISIBLE|HOST_COHERENT
 * for mapped buffers, DEVICE_LOCAL for images. Returns type_count when
 * nothing qualifies. */
static uint32_t pick_memory(const scene_mem_props_t *mem, uint32_t bits,
    uint32_t prefer, uint32_t require) {
    uint32_t fallback = mem->type_count;
    for (uint32_t i = 0; i < mem->type_count; ++i) {
        if (!(bits & (1u << i))) {
            continue;
        }
        if ((mem->types[i].property_flags & require) != require) {
            continue;
        }
        if ((mem->types[i].property_flags & prefer) == prefer) {
            return i;
        }
        if (fallback == mem->type_count) {
            fallback = i;
        }
    }
    return fallback;
}

typedef struct {
    void *loader;
    jvk_fns_t fns;
    jvk_instance_t instance;
    jvk_physical_device_t physical;
    jvk_device_t device;
    jvk_queue_t queue;
    uint32_t queue_family;
} scene_device_t;

static void scene_device_teardown(scene_device_t *dev) {
    if (!dev) {
        return;
    }
    if (dev->device) {
        dev->fns.device_wait_idle(dev->device);
        dev->fns.destroy_device(dev->device, NULL);
    }
    if (dev->instance) {
        dev->fns.destroy_instance(dev->instance, NULL);
    }
    if (dev->loader) {
        dlclose(dev->loader);
    }
    memset(dev, 0, sizeof(*dev));
}

/* Bring up instance, discrete-GPU-preferring physical device, one graphics
 * queue and a logical device. False when no driver or no graphics queue. */
static bool scene_device_init(scene_device_t *dev) {
    memset(dev, 0, sizeof(*dev));
    dev->loader = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!dev->loader) {
        dev->loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
    }
    if (!dev->loader || !jvk_load_fns(dev->loader, &dev->fns)) {
        scene_device_teardown(dev);
        return false;
    }
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
    if (dev->fns.create_instance(&info, NULL, &dev->instance) != JVK_SUCCESS ||
        !dev->instance) {
        scene_device_teardown(dev);
        return false;
    }
    uint32_t count = 0;
    if (dev->fns.enumerate_physical_devices(dev->instance, &count, NULL) !=
            JVK_SUCCESS ||
        !count) {
        scene_device_teardown(dev);
        return false;
    }
    jvk_physical_device_t *list = scene_alloc(count * sizeof(*list));
    unsigned char *scratch =
        scene_alloc(JVK_PHYSICAL_DEVICE_PROPERTIES_SCRATCH);
    if (!list || !scratch) {
        scene_free(list);
        scene_free(scratch);
        scene_device_teardown(dev);
        return false;
    }
    jvk_physical_device_t pick = NULL;
    if (dev->fns.enumerate_physical_devices(dev->instance, &count, list) ==
        JVK_SUCCESS) {
        for (uint32_t i = 0; i < count; ++i) {
            memset(scratch, 0, JVK_PHYSICAL_DEVICE_PROPERTIES_SCRATCH);
            dev->fns.get_physical_device_properties(list[i], scratch);
            int32_t type = 0;
            memcpy(&type, scratch + JVK_DEVICE_TYPE_OFFSET, sizeof(type));
            if (!pick || type == JVK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                pick = list[i];
            }
            if (type == JVK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
                break;
            }
        }
    }
    scene_free(scratch);
    scene_free(list);
    if (!pick) {
        scene_device_teardown(dev);
        return false;
    }
    dev->physical = pick;
    uint32_t family_count = 0;
    dev->fns.get_physical_device_queue_family_properties(dev->physical,
        &family_count, NULL);
    jvk_queue_family_properties_t *families =
        scene_alloc(family_count * sizeof(*families));
    if (!family_count || !families) {
        scene_free(families);
        scene_device_teardown(dev);
        return false;
    }
    dev->fns.get_physical_device_queue_family_properties(dev->physical,
        &family_count, families);
    uint32_t family = family_count;
    for (uint32_t i = 0; i < family_count; ++i) {
        if (families[i].queue_flags & JVK_QUEUE_GRAPHICS_BIT) {
            family = i;
            break;
        }
    }
    scene_free(families);
    if (family == family_count) {
        scene_device_teardown(dev);
        return false;
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
    jvk_device_create_info_t device_info = {
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
    if (dev->fns.create_device(dev->physical, &device_info, NULL,
            &dev->device) != JVK_SUCCESS ||
        !dev->device) {
        scene_device_teardown(dev);
        return false;
    }
    dev->fns.get_device_queue(dev->device, family, 0, &dev->queue);
    if (!dev->queue) {
        scene_device_teardown(dev);
        return false;
    }
    return true;
}

typedef struct {
    jvk_buffer_t buffer;
    jvk_device_memory_t memory;
    void *mapped;
    bool coherent;
    jvk_device_size_t size;
} scene_buffer_t;

static bool scene_buffer_create(scene_device_t *dev,
    const scene_mem_props_t *mem, jvk_device_size_t bytes, uint32_t usage,
    scene_buffer_t *out) {
    memset(out, 0, sizeof(*out));
    jvk_buffer_create_info_t info = {
        .s_type = JVK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .size = bytes,
        .usage = usage,
        .sharing_mode = JVK_SHARING_MODE_EXCLUSIVE,
        .queue_family_index_count = 0,
        .p_queue_family_indices = NULL,
    };
    if (dev->fns.create_buffer(dev->device, &info, NULL, &out->buffer) !=
            JVK_SUCCESS ||
        !out->buffer) {
        return false;
    }
    jvk_memory_requirements_t req = {0};
    dev->fns.get_buffer_memory_requirements(dev->device, out->buffer, &req);
    out->size = req.size;
    uint32_t picked = pick_memory(mem, req.memory_type_bits,
        JVK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            JVK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        JVK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    if (picked == mem->type_count) {
        return false;
    }
    out->coherent = scene_features(mem->types[picked].property_flags,
        JVK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    jvk_memory_allocate_info_t alloc = {
        .s_type = JVK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .p_next = NULL,
        .allocation_size = req.size,
        .memory_type_index = picked,
    };
    if (dev->fns.allocate_memory(dev->device, &alloc, NULL, &out->memory) !=
            JVK_SUCCESS ||
        !out->memory ||
        dev->fns.bind_buffer_memory(dev->device, out->buffer, out->memory,
            0) != JVK_SUCCESS ||
        dev->fns.map_memory(dev->device, out->memory, 0, JVK_WHOLE_SIZE, 0,
            &out->mapped) != JVK_SUCCESS ||
        !out->mapped) {
        return false;
    }
    return true;
}

static void scene_buffer_flush(scene_device_t *dev, const scene_buffer_t *buf) {
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

static void scene_buffer_invalidate(scene_device_t *dev,
    const scene_buffer_t *buf) {
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

static void scene_buffer_destroy(scene_device_t *dev, scene_buffer_t *buf) {
    if (buf->mapped) {
        dev->fns.unmap_memory(dev->device, buf->memory);
    }
    if (buf->buffer) {
        dev->fns.destroy_buffer(dev->device, buf->buffer, NULL);
    }
    if (buf->memory) {
        dev->fns.free_memory(dev->device, buf->memory, NULL);
    }
    memset(buf, 0, sizeof(*buf));
}

/* Renders pre-transformed draws into `out_rgba` (tightly packed RGBA8,
 * y-down rows). False on any failure with output untouched. */
bool jvk_scene_render(const jvk_scene_draw_t *draws, uint32_t draw_count,
    const float vp[16], const float sun_xyz[3], const float clear_rgb[3],
    uint32_t width, uint32_t height, uint8_t *out_rgba) {
    if (!draws || !draw_count || draw_count > SCENE_MAX_DRAWS || !vp ||
        !sun_xyz || !clear_rgb || !width || !height || width > 2048 ||
        height > 2048 || !out_rgba) {
        return false;
    }
    uint64_t total_verts = 0;
    for (uint32_t i = 0; i < draw_count; ++i) {
        if (!draws[i].vertices || !draws[i].vertex_count ||
            draws[i].vertex_count % 3u) {
            return false;
        }
        total_verts += draws[i].vertex_count;
        if (total_verts > SCENE_MAX_VERTS) {
            return false;
        }
    }
    scene_device_t dev;
    const char *stage = "args";
    bool ok = false;
    /* Transient render objects; every exit path destroys what was made.
     * Declared before device init so the early failure path below only
     * ever reads initialized handles. */
    jvk_command_pool_t pool = JVK_NULL_HANDLE;
    jvk_command_buffer_t cmd = NULL;
    jvk_fence_t fence = JVK_NULL_HANDLE;
    jvk_image_t color_image = JVK_NULL_HANDLE;
    jvk_image_t depth_image = JVK_NULL_HANDLE;
    jvk_device_memory_t color_memory = JVK_NULL_HANDLE;
    jvk_device_memory_t depth_memory = JVK_NULL_HANDLE;
    jvk_image_view_t color_view = JVK_NULL_HANDLE;
    jvk_image_view_t depth_view = JVK_NULL_HANDLE;
    jvk_render_pass_t pass = JVK_NULL_HANDLE;
    jvk_framebuffer_t framebuffer = JVK_NULL_HANDLE;
    jvk_shader_module_t vert_module = JVK_NULL_HANDLE;
    jvk_shader_module_t frag_module = JVK_NULL_HANDLE;
    jvk_descriptor_set_layout_t set_layout = JVK_NULL_HANDLE;
    jvk_pipeline_layout_t pipe_layout = JVK_NULL_HANDLE;
    jvk_pipeline_t pipeline = JVK_NULL_HANDLE;
    jvk_descriptor_pool_t desc_pool = JVK_NULL_HANDLE;
    scene_buffer_t vertex_buf = {0};
    scene_buffer_t readback_buf = {0};
    scene_buffer_t *ubo_bufs = NULL;
    jvk_descriptor_set_t *sets = NULL;
    scene_mem_props_t mem;
    memset(&mem, 0, sizeof(mem));
    memset(&dev, 0, sizeof(dev));
    if (!scene_device_init(&dev)) {
        stage = "device";
        goto report;
    }
    stage = "setup";
    dev.fns.get_physical_device_memory_properties(dev.physical, &mem);
    if (!mem.type_count || mem.type_count > 32) {
        goto done;
    }
    stage = "formats";
    /* Color must render and copy out; depth must attach. Fail closed. */
    {
        unsigned char feat[JVK_FORMAT_PROPERTIES_SCRATCH];
        memset(feat, 0, sizeof(feat));
        dev.fns.get_physical_device_format_properties(dev.physical,
            JVK_FORMAT_R8G8B8A8_UNORM, feat);
        uint32_t optimal = 0;
        memcpy(&optimal, feat + JVK_FORMAT_FEATURES_OPTIMAL_OFFSET,
            sizeof(optimal));
        if (scene_debug() && !scene_features(optimal,
                JVK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            fprintf(stderr,
                "[jfx-vk-scene] rgba8888 optimal features=0x%08x\n", optimal);
        }
        if (!scene_features(optimal, JVK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
            stage = "formats-color";
            goto done;
        }
        memset(feat, 0, sizeof(feat));
        dev.fns.get_physical_device_format_properties(dev.physical,
            JVK_FORMAT_D32_SFLOAT, feat);
        memcpy(&optimal, feat + JVK_FORMAT_FEATURES_OPTIMAL_OFFSET,
            sizeof(optimal));
        if (scene_debug() && !scene_features(optimal,
                JVK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            fprintf(stderr, "[jfx-vk-scene] d32 optimal features=0x%08x\n",
                optimal);
        }
        if (!scene_features(optimal,
                JVK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
            stage = "formats-depth";
            goto done;
        }
    }
    stage = "command";
    jvk_command_pool_create_info_t pool_info = {
        .s_type = JVK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
        .queue_family_index = dev.queue_family,
    };
    if (dev.fns.create_command_pool(dev.device, &pool_info, NULL, &pool) !=
            JVK_SUCCESS ||
        !pool) {
        goto done;
    }
    jvk_command_buffer_allocate_info_t cmd_info = {
        .s_type = JVK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .p_next = NULL,
        .command_pool = pool,
        .level = JVK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .command_buffer_count = 1,
    };
    if (dev.fns.allocate_command_buffers(dev.device, &cmd_info, &cmd) !=
            JVK_SUCCESS ||
        !cmd) {
        goto done;
    }
    jvk_fence_create_info_t fence_info = {
        .s_type = JVK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .p_next = NULL,
        .flags = 0,
    };
    if (dev.fns.create_fence(dev.device, &fence_info, NULL, &fence) !=
            JVK_SUCCESS ||
        !fence) {
        goto done;
    }
    /* Color + depth images in device-local memory. */
    const struct {
        jvk_image_t *image;
        jvk_device_memory_t *memory;
        int32_t format;
        uint32_t usage;
    } images[] = {
        {&color_image, &color_memory, JVK_FORMAT_R8G8B8A8_UNORM,
            JVK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                JVK_IMAGE_USAGE_TRANSFER_SRC_BIT},
        {&depth_image, &depth_memory, JVK_FORMAT_D32_SFLOAT,
            JVK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT},
    };
    stage = "images";
    for (size_t i = 0; i < sizeof(images) / sizeof(*images); ++i) {
        jvk_image_create_info_t image_info = {
            .s_type = JVK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .image_type = JVK_IMAGE_TYPE_2D,
            .format = images[i].format,
            .extent = {width, height, 1},
            .mip_levels = 1,
            .array_layers = 1,
            .samples = JVK_SAMPLE_COUNT_1_BIT,
            .tiling = JVK_IMAGE_TILING_OPTIMAL,
            .usage = images[i].usage,
            .sharing_mode = JVK_SHARING_MODE_EXCLUSIVE,
            .queue_family_index_count = 0,
            .p_queue_family_indices = NULL,
            .initial_layout = JVK_IMAGE_LAYOUT_UNDEFINED,
        };
        if (dev.fns.create_image(dev.device, &image_info, NULL,
                images[i].image) != JVK_SUCCESS ||
            !*images[i].image) {
            goto done;
        }
        jvk_memory_requirements_t req = {0};
        dev.fns.get_image_memory_requirements(dev.device, *images[i].image,
            &req);
        uint32_t picked = pick_memory(&mem, req.memory_type_bits,
            JVK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            JVK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (picked == mem.type_count) {
            picked = pick_memory(&mem, req.memory_type_bits, 0, 0);
        }
        if (picked == mem.type_count) {
            goto done;
        }
        jvk_memory_allocate_info_t alloc = {
            .s_type = JVK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .p_next = NULL,
            .allocation_size = req.size,
            .memory_type_index = picked,
        };
        if (dev.fns.allocate_memory(dev.device, &alloc, NULL,
                images[i].memory) != JVK_SUCCESS ||
            !*images[i].memory ||
            dev.fns.bind_image_memory(dev.device, *images[i].image,
                *images[i].memory, 0) != JVK_SUCCESS) {
            goto done;
        }
    }
    stage = "views";
    /* Views. */
    {
        jvk_image_view_create_info_t view_info = {
            .s_type = JVK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .image = color_image,
            .view_type = JVK_IMAGE_VIEW_TYPE_2D,
            .format = JVK_FORMAT_R8G8B8A8_UNORM,
            .components = {JVK_COMPONENT_SWIZZLE_IDENTITY,
                JVK_COMPONENT_SWIZZLE_IDENTITY, JVK_COMPONENT_SWIZZLE_IDENTITY,
                JVK_COMPONENT_SWIZZLE_IDENTITY},
            .subresource_range = {JVK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        if (dev.fns.create_image_view(dev.device, &view_info, NULL,
                &color_view) != JVK_SUCCESS ||
            !color_view) {
            goto done;
        }
        view_info.image = depth_image;
        view_info.format = JVK_FORMAT_D32_SFLOAT;
        view_info.subresource_range.aspect_mask = JVK_IMAGE_ASPECT_DEPTH_BIT;
        if (dev.fns.create_image_view(dev.device, &view_info, NULL,
                &depth_view) != JVK_SUCCESS ||
            !depth_view) {
            goto done;
        }
    }
    stage = "pass";
    /* Render pass: color clears and stores for the copy-out, depth clears. */
    {
        jvk_attachment_description_t attachments[2] = {
            {
                .flags = 0,
                .format = JVK_FORMAT_R8G8B8A8_UNORM,
                .samples = JVK_SAMPLE_COUNT_1_BIT,
                .load_op = JVK_ATTACHMENT_LOAD_OP_CLEAR,
                .store_op = JVK_ATTACHMENT_STORE_OP_STORE,
                .stencil_load_op = JVK_ATTACHMENT_LOAD_OP_CLEAR,
                .stencil_store_op = JVK_ATTACHMENT_STORE_OP_STORE,
                .initial_layout = JVK_IMAGE_LAYOUT_UNDEFINED,
                .final_layout = JVK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            },
            {
                .flags = 0,
                .format = JVK_FORMAT_D32_SFLOAT,
                .samples = JVK_SAMPLE_COUNT_1_BIT,
                .load_op = JVK_ATTACHMENT_LOAD_OP_CLEAR,
                .store_op = JVK_ATTACHMENT_STORE_OP_STORE,
                .stencil_load_op = JVK_ATTACHMENT_LOAD_OP_CLEAR,
                .stencil_store_op = JVK_ATTACHMENT_STORE_OP_STORE,
                .initial_layout = JVK_IMAGE_LAYOUT_UNDEFINED,
                .final_layout =
                    JVK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        };
        jvk_attachment_reference_t color_ref = {
            .attachment = 0,
            .layout = JVK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        };
        jvk_attachment_reference_t depth_ref = {
            .attachment = 1,
            .layout = JVK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
        };
        jvk_subpass_description_t subpass = {
            .flags = 0,
            .pipeline_bind_point = JVK_PIPELINE_BIND_POINT_GRAPHICS,
            .input_attachment_count = 0,
            .p_input_attachments = NULL,
            .color_attachment_count = 1,
            .p_color_attachments = &color_ref,
            .p_resolve_attachments = NULL,
            .p_depth_stencil_attachment = &depth_ref,
            .preserve_attachment_count = 0,
            .p_preserve_attachments = NULL,
        };
        jvk_subpass_dependency_t dependency = {
            .src_subpass = JVK_SUBPASS_EXTERNAL,
            .dst_subpass = 0,
            .src_stage_mask = JVK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            .dst_stage_mask = JVK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                JVK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .src_access_mask = 0,
            .dst_access_mask = JVK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                JVK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dependency_flags = 0,
        };
        jvk_render_pass_create_info_t pass_info = {
            .s_type = JVK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .attachment_count = 2,
            .p_attachments = attachments,
            .subpass_count = 1,
            .p_subpasses = &subpass,
            .dependency_count = 1,
            .p_dependencies = &dependency,
        };
        if (dev.fns.create_render_pass(dev.device, &pass_info, NULL, &pass) !=
                JVK_SUCCESS ||
            !pass) {
            goto done;
        }
        uint64_t views[2] = {color_view, depth_view};
        jvk_framebuffer_create_info_t fb_info = {
            .s_type = JVK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .render_pass = pass,
            .attachment_count = 2,
            .p_attachments = views,
            .width = width,
            .height = height,
            .layers = 1,
        };
        if (dev.fns.create_framebuffer(dev.device, &fb_info, NULL,
                &framebuffer) != JVK_SUCCESS ||
            !framebuffer) {
            goto done;
        }
    }
    stage = "pipeline";
    /* Shaders, descriptor layout (one UBO), pipeline layout, pipeline. */
    {
        jvk_shader_module_create_info_t module_info = {
            .s_type = JVK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .code_size = sizeof(kSceneVert),
            .p_code = kSceneVert,
        };
        if (dev.fns.create_shader_module(dev.device, &module_info, NULL,
                &vert_module) != JVK_SUCCESS ||
            !vert_module) {
            goto done;
        }
        module_info.code_size = sizeof(kSceneFrag);
        module_info.p_code = kSceneFrag;
        if (dev.fns.create_shader_module(dev.device, &module_info, NULL,
                &frag_module) != JVK_SUCCESS ||
            !frag_module) {
            goto done;
        }
        jvk_descriptor_set_layout_binding_t binding = {
            .binding = 0,
            .descriptor_type = JVK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptor_count = 1,
            .stage_flags = JVK_SHADER_STAGE_VERTEX_BIT,
            .p_immutable_samplers = NULL,
        };
        jvk_descriptor_set_layout_create_info_t layout_info = {
            .s_type = JVK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .binding_count = 1,
            .p_bindings = &binding,
        };
        if (dev.fns.create_descriptor_set_layout(dev.device, &layout_info,
                NULL, &set_layout) != JVK_SUCCESS ||
            !set_layout) {
            goto done;
        }
        jvk_pipeline_layout_create_info_t pipe_layout_info = {
            .s_type = JVK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .set_layout_count = 1,
            .p_set_layouts = &set_layout,
            .push_constant_range_count = 0,
            .p_push_constant_ranges = NULL,
        };
        if (dev.fns.create_pipeline_layout(dev.device, &pipe_layout_info,
                NULL, &pipe_layout) != JVK_SUCCESS ||
            !pipe_layout) {
            goto done;
        }
        jvk_pipeline_shader_stage_create_info_t stages[2] = {
            {
                .s_type = JVK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .p_next = NULL,
                .flags = 0,
                .stage = JVK_SHADER_STAGE_VERTEX_BIT,
                .module = vert_module,
                .p_name = "main",
                .p_specialization_info = NULL,
            },
            {
                .s_type = JVK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .p_next = NULL,
                .flags = 0,
                .stage = JVK_SHADER_STAGE_FRAGMENT_BIT,
                .module = frag_module,
                .p_name = "main",
                .p_specialization_info = NULL,
            },
        };
        jvk_vertex_input_binding_description_t vbinding = {
            .binding = 0,
            .stride = SCENE_VERT_STRIDE,
            .input_rate = JVK_VERTEX_INPUT_RATE_VERTEX,
        };
        jvk_vertex_input_attribute_description_t vattrs[2] = {
            {.location = 0,
                .binding = 0,
                .format = JVK_FORMAT_R32G32B32_SFLOAT,
                .offset = 0},
            {.location = 1,
                .binding = 0,
                .format = JVK_FORMAT_R32G32B32_SFLOAT,
                .offset = 12},
        };
        jvk_pipeline_vertex_input_state_create_info_t vertex_input = {
            .s_type =
                JVK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .vertex_binding_description_count = 1,
            .p_vertex_binding_descriptions = &vbinding,
            .vertex_attribute_description_count = 2,
            .p_vertex_attribute_descriptions = vattrs,
        };
        jvk_pipeline_input_assembly_state_create_info_t assembly = {
            .s_type =
                JVK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .topology = JVK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
            .primitive_restart_enable = 0,
        };
        jvk_viewport_t viewport = {
            .x = 0.0f,
            .y = 0.0f,
            .width = (float)width,
            .height = (float)height,
            .min_depth = 0.0f,
            .max_depth = 1.0f,
        };
        jvk_rect_2d_t scissor = {
            .x = 0, .y = 0, .width = width, .height = height,
        };
        jvk_pipeline_viewport_state_create_info_t viewport_state = {
            .s_type = JVK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .viewport_count = 1,
            .p_viewports = &viewport,
            .scissor_count = 1,
            .p_scissors = &scissor,
        };
        jvk_pipeline_rasterization_state_create_info_t raster = {
            .s_type =
                JVK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .depth_clamp_enable = 0,
            .rasterizer_discard_enable = 0,
            .polygon_mode = JVK_POLYGON_MODE_FILL,
            .cull_mode = JVK_CULL_MODE_NONE,
            .front_face = JVK_FRONT_FACE_COUNTER_CLOCKWISE,
            .depth_bias_enable = 0,
            .depth_bias_constant_factor = 0.0f,
            .depth_bias_clamp = 0.0f,
            .depth_bias_slope_factor = 0.0f,
            .line_width = 1.0f,
        };
        jvk_pipeline_multisample_state_create_info_t multisample = {
            .s_type = JVK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .rasterization_samples = JVK_SAMPLE_COUNT_1_BIT,
            .sample_shading_enable = 0,
            .min_sample_shading = 0.0f,
            .p_sample_mask = NULL,
            .alpha_to_coverage_enable = 0,
            .alpha_to_one_enable = 0,
        };
        jvk_pipeline_depth_stencil_state_create_info_t depth_stencil = {
            .s_type =
                JVK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .depth_test_enable = 1,
            .depth_write_enable = 1,
            .depth_compare_op = JVK_COMPARE_OP_LESS,
            .depth_bounds_test_enable = 0,
            .stencil_test_enable = 0,
            .front = {JVK_STENCIL_OP_KEEP, JVK_STENCIL_OP_KEEP,
                JVK_STENCIL_OP_KEEP, JVK_COMPARE_OP_LESS, 0, 0, 0},
            .back = {JVK_STENCIL_OP_KEEP, JVK_STENCIL_OP_KEEP,
                JVK_STENCIL_OP_KEEP, JVK_COMPARE_OP_LESS, 0, 0, 0},
            .min_depth_bounds = 0.0f,
            .max_depth_bounds = 1.0f,
        };
        jvk_pipeline_color_blend_attachment_state_t blend_attachment = {
            .blend_enable = 0,
            .src_color_blend_factor = JVK_BLEND_FACTOR_ONE,
            .dst_color_blend_factor = JVK_BLEND_FACTOR_ZERO,
            .color_blend_op = JVK_BLEND_OP_ADD,
            .src_alpha_blend_factor = JVK_BLEND_FACTOR_ONE,
            .dst_alpha_blend_factor = JVK_BLEND_FACTOR_ZERO,
            .alpha_blend_op = JVK_BLEND_OP_ADD,
            .color_write_mask = JVK_COLOR_COMPONENT_R_BIT |
                JVK_COLOR_COMPONENT_G_BIT | JVK_COLOR_COMPONENT_B_BIT |
                JVK_COLOR_COMPONENT_A_BIT,
        };
        jvk_pipeline_color_blend_state_create_info_t blend = {
            .s_type = JVK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .logic_op_enable = 0,
            .logic_op = 0,
            .attachment_count = 1,
            .p_attachments = &blend_attachment,
            .blend_constants = {0.0f, 0.0f, 0.0f, 0.0f},
        };
        jvk_graphics_pipeline_create_info_t pipe_info = {
            .s_type = JVK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .stage_count = 2,
            .p_stages = stages,
            .p_vertex_input_state = &vertex_input,
            .p_input_assembly_state = &assembly,
            .p_tessellation_state = NULL,
            .p_viewport_state = &viewport_state,
            .p_rasterization_state = &raster,
            .p_multisample_state = &multisample,
            .p_depth_stencil_state = &depth_stencil,
            .p_color_blend_state = &blend,
            .p_dynamic_state = NULL,
            .layout = pipe_layout,
            .render_pass = pass,
            .subpass = 0,
            .base_pipeline_handle = JVK_NULL_HANDLE,
            .base_pipeline_index = -1,
        };
        if (dev.fns.create_graphics_pipelines(dev.device, JVK_NULL_HANDLE, 1,
                &pipe_info, NULL, &pipeline) != JVK_SUCCESS ||
            !pipeline) {
            goto done;
        }
    }
    stage = "descriptors";
    /* Descriptor pool + one set per draw. */
    {
        jvk_descriptor_pool_size_t pool_size = {
            .descriptor_type = JVK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .descriptor_count = draw_count,
        };
        jvk_descriptor_pool_create_info_t dpool_info = {
            .s_type = JVK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .p_next = NULL,
            .flags = 0,
            .max_sets = draw_count,
            .pool_size_count = 1,
            .p_pool_sizes = &pool_size,
        };
        if (dev.fns.create_descriptor_pool(dev.device, &dpool_info, NULL,
                &desc_pool) != JVK_SUCCESS ||
            !desc_pool) {
            goto done;
        }
    }
    ubo_bufs = scene_alloc(draw_count * sizeof(*ubo_bufs));
    sets = scene_alloc(draw_count * sizeof(*sets));
    if (!ubo_bufs || !sets) {
        goto done;
    }
    memset(ubo_bufs, 0, draw_count * sizeof(*ubo_bufs));
    stage = "staging";
    /* Staging: one vertex buffer, per-draw UBOs, one readback buffer. */
    {
        jvk_device_size_t vert_bytes = (jvk_device_size_t)total_verts * 6u * 4u;
        if (!scene_buffer_create(&dev, &mem, vert_bytes,
                JVK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vertex_buf)) {
            goto done;
        }
        float *dst = (float *)vertex_buf.mapped;
        for (uint32_t i = 0; i < draw_count; ++i) {
            size_t floats = (size_t)draws[i].vertex_count * 6u;
            memcpy(dst, draws[i].vertices, floats * sizeof(float));
            dst += floats;
        }
        scene_buffer_flush(&dev, &vertex_buf);
        for (uint32_t i = 0; i < draw_count; ++i) {
            if (!scene_buffer_create(&dev, &mem, SCENE_UBO_SIZE,
                    JVK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &ubo_bufs[i])) {
                goto done;
            }
            unsigned char *ubo = (unsigned char *)ubo_bufs[i].mapped;
            memcpy(ubo + 0, vp, 64);
            memcpy(ubo + 64, draws[i].model, 64);
            memcpy(ubo + 128, draws[i].nmat, 64);
            memcpy(ubo + 192, draws[i].color, 16);
            memcpy(ubo + 208, sun_xyz, 12);
            memset(ubo + 220, 0, 4);
            scene_buffer_flush(&dev, &ubo_bufs[i]);
            jvk_descriptor_set_allocate_info_t alloc_info = {
                .s_type = JVK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
                .p_next = NULL,
                .descriptor_pool = desc_pool,
                .descriptor_set_count = 1,
                .p_set_layouts = &set_layout,
            };
            if (dev.fns.allocate_descriptor_sets(dev.device, &alloc_info,
                    &sets[i]) != JVK_SUCCESS ||
                !sets[i]) {
                goto done;
            }
            jvk_descriptor_buffer_info_t buffer_info = {
                .buffer = ubo_bufs[i].buffer,
                .offset = 0,
                .range = SCENE_UBO_SIZE,
            };
            jvk_write_descriptor_set_t write = {
                .s_type = JVK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .p_next = NULL,
                .dst_set = sets[i],
                .dst_binding = 0,
                .dst_array_element = 0,
                .descriptor_count = 1,
                .descriptor_type = JVK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .p_image_info = NULL,
                .p_buffer_info = &buffer_info,
                .p_texel_buffer_view = NULL,
            };
            dev.fns.update_descriptor_sets(dev.device, 1, &write, 0, NULL);
        }
        if (!scene_buffer_create(&dev, &mem,
                (jvk_device_size_t)width * height * 4u,
                JVK_BUFFER_USAGE_TRANSFER_DST_BIT, &readback_buf)) {
            goto done;
        }
    }
    stage = "submit";
    /* Record, submit, wait, read back. */
    {
        jvk_command_buffer_begin_info_t begin = {
            .s_type = JVK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .p_next = NULL,
            .flags = JVK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .p_inheritance_info = NULL,
        };
        if (dev.fns.begin_command_buffer(cmd, &begin) != JVK_SUCCESS) {
            goto done;
        }
        jvk_image_memory_barrier_t to_attach[2] = {
            {
                .s_type = JVK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .p_next = NULL,
                .src_access_mask = 0,
                .dst_access_mask = JVK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .old_layout = JVK_IMAGE_LAYOUT_UNDEFINED,
                .new_layout = JVK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .src_queue_family_index = JVK_QUEUE_FAMILY_IGNORED,
                .dst_queue_family_index = JVK_QUEUE_FAMILY_IGNORED,
                .image = color_image,
                .subresource_range = {JVK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
            },
            {
                .s_type = JVK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .p_next = NULL,
                .src_access_mask = 0,
                .dst_access_mask =
                    JVK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                .old_layout = JVK_IMAGE_LAYOUT_UNDEFINED,
                .new_layout =
                    JVK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                .src_queue_family_index = JVK_QUEUE_FAMILY_IGNORED,
                .dst_queue_family_index = JVK_QUEUE_FAMILY_IGNORED,
                .image = depth_image,
                .subresource_range = {JVK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1},
            },
        };
        dev.fns.cmd_pipeline_barrier(cmd,
            JVK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            JVK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                JVK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            0, 0, NULL, 0, NULL, 2, to_attach);
        jvk_clear_value_t clears[2];
        clears[0].color[0] = clear_rgb[0];
        clears[0].color[1] = clear_rgb[1];
        clears[0].color[2] = clear_rgb[2];
        clears[0].color[3] = 1.0f;
        clears[1].depth_stencil.depth = 1.0f;
        clears[1].depth_stencil.stencil = 0;
        jvk_render_pass_begin_info_t pass_begin = {
            .s_type = JVK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .p_next = NULL,
            .render_pass = pass,
            .framebuffer = framebuffer,
            .render_area = {0, 0, width, height},
            .clear_value_count = 2,
            .p_clear_values = clears,
        };
        dev.fns.cmd_begin_render_pass(cmd, &pass_begin,
            JVK_SUBPASS_CONTENTS_INLINE);
        dev.fns.cmd_bind_pipeline(cmd, JVK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline);
        uint64_t vert_offset = 0;
        for (uint32_t i = 0; i < draw_count; ++i) {
            jvk_device_size_t offset = (jvk_device_size_t)vert_offset;
            dev.fns.cmd_bind_vertex_buffers(cmd, 0, 1, &vertex_buf.buffer,
                &offset);
            dev.fns.cmd_bind_descriptor_sets(cmd,
                JVK_PIPELINE_BIND_POINT_GRAPHICS, pipe_layout, 0, 1, &sets[i],
                0, NULL);
            dev.fns.cmd_draw(cmd, draws[i].vertex_count, 1, 0, 0);
            vert_offset += (uint64_t)draws[i].vertex_count * 6u * 4u;
        }
        dev.fns.cmd_end_render_pass(cmd);
        jvk_image_memory_barrier_t to_copy = {
            .s_type = JVK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .p_next = NULL,
            .src_access_mask = JVK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
            .dst_access_mask = JVK_ACCESS_TRANSFER_READ_BIT,
            .old_layout = JVK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .new_layout = JVK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            .src_queue_family_index = JVK_QUEUE_FAMILY_IGNORED,
            .dst_queue_family_index = JVK_QUEUE_FAMILY_IGNORED,
            .image = color_image,
            .subresource_range = {JVK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
        };
        dev.fns.cmd_pipeline_barrier(cmd,
            JVK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            JVK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &to_copy);
        jvk_buffer_image_copy_t region = {
            .buffer_offset = 0,
            .buffer_row_length = 0,
            .buffer_image_height = 0,
            .image_subresource = {JVK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1},
            .image_offset = {0, 0, 0},
            .image_extent = {width, height, 1},
        };
        dev.fns.cmd_copy_image_to_buffer(cmd, color_image,
            JVK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_buf.buffer, 1,
            &region);
        if (dev.fns.end_command_buffer(cmd) != JVK_SUCCESS) {
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
        if (dev.fns.queue_submit(dev.queue, 1, &submit, fence) !=
                JVK_SUCCESS ||
            dev.fns.wait_for_fences(dev.device, 1, &fence, 1,
                SCENE_FENCE_TIMEOUT_NS) != JVK_SUCCESS) {
            goto done;
        }
        scene_buffer_invalidate(&dev, &readback_buf);
        memcpy(out_rgba, readback_buf.mapped, (size_t)width * height * 4u);
        ok = true;
    }
done:
report:
    if (ubo_bufs) {
        for (uint32_t i = 0; i < draw_count; ++i) {
            scene_buffer_destroy(&dev, &ubo_bufs[i]);
        }
    }
    scene_free(ubo_bufs);
    scene_free(sets);
    scene_buffer_destroy(&dev, &vertex_buf);
    scene_buffer_destroy(&dev, &readback_buf);
    if (pipeline) {
        dev.fns.destroy_pipeline(dev.device, pipeline, NULL);
    }
    if (desc_pool) {
        dev.fns.destroy_descriptor_pool(dev.device, desc_pool, NULL);
    }
    if (pipe_layout) {
        dev.fns.destroy_pipeline_layout(dev.device, pipe_layout, NULL);
    }
    if (set_layout) {
        dev.fns.destroy_descriptor_set_layout(dev.device, set_layout, NULL);
    }
    if (vert_module) {
        dev.fns.destroy_shader_module(dev.device, vert_module, NULL);
    }
    if (frag_module) {
        dev.fns.destroy_shader_module(dev.device, frag_module, NULL);
    }
    if (framebuffer) {
        dev.fns.destroy_framebuffer(dev.device, framebuffer, NULL);
    }
    if (pass) {
        dev.fns.destroy_render_pass(dev.device, pass, NULL);
    }
    if (color_view) {
        dev.fns.destroy_image_view(dev.device, color_view, NULL);
    }
    if (depth_view) {
        dev.fns.destroy_image_view(dev.device, depth_view, NULL);
    }
    if (color_image) {
        dev.fns.destroy_image(dev.device, color_image, NULL);
    }
    if (depth_image) {
        dev.fns.destroy_image(dev.device, depth_image, NULL);
    }
    if (color_memory) {
        dev.fns.free_memory(dev.device, color_memory, NULL);
    }
    if (depth_memory) {
        dev.fns.free_memory(dev.device, depth_memory, NULL);
    }
    if (fence) {
        dev.fns.destroy_fence(dev.device, fence, NULL);
    }
    if (cmd) {
        dev.fns.free_command_buffers(dev.device, pool, 1, &cmd);
    }
    if (pool) {
        dev.fns.destroy_command_pool(dev.device, pool, NULL);
    }
    if (!ok) {
        const char *debug = getenv("JFX_VK_SCENE_DEBUG");
        if (debug && debug[0]) {
            fprintf(stderr, "[jfx-vk-scene] render failed at stage: %s\n", stage);
        }
    }
    scene_device_teardown(&dev);
    return ok;
}
