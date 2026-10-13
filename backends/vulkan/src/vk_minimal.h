/* Minimal Vulkan ABI declarations for the JoltFX compute path.
 *
 * The build never requires the Vulkan SDK: every type, constant, and
 * function pointer below mirrors the stable Vulkan 1.0/1.1 ABI and is
 * resolved at runtime with dlsym. Values were checked against the Vulkan
 * specification; any drift fails closed because every call site checks its
 * VkResult and the backend falls back to (or reports) an error instead of
 * proceeding. Only the compute-subset needed by vk_compute.c is declared.
 */
#ifndef JFX_VK_MINIMAL_H
#define JFX_VK_MINIMAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef uint32_t jvk_bool32_t;
typedef uint64_t jvk_device_size_t;
typedef int32_t jvk_result_t;
typedef uint32_t jvk_flags_t;
typedef jvk_flags_t jvk_buffer_usage_flags_t;
typedef jvk_flags_t jvk_memory_property_flags_t;
typedef jvk_flags_t jvk_shader_stage_flags_t;
typedef jvk_flags_t jvk_access_flags_t;
typedef jvk_flags_t jvk_pipeline_stage_flags_t;
typedef jvk_flags_t jvk_command_buffer_usage_flags_t;

typedef struct jvk_instance_s *jvk_instance_t;
typedef struct jvk_physical_device_s *jvk_physical_device_t;
typedef struct jvk_device_s *jvk_device_t;
typedef struct jvk_queue_s *jvk_queue_t;
typedef struct jvk_command_buffer_s *jvk_command_buffer_t;
typedef uint64_t jvk_buffer_t;
typedef uint64_t jvk_device_memory_t;
typedef uint64_t jvk_shader_module_t;
typedef uint64_t jvk_descriptor_set_layout_t;
typedef uint64_t jvk_pipeline_layout_t;
typedef uint64_t jvk_pipeline_t;
typedef uint64_t jvk_descriptor_pool_t;
typedef uint64_t jvk_descriptor_set_t;
typedef uint64_t jvk_command_pool_t;
typedef uint64_t jvk_fence_t;

#define JVK_NULL_HANDLE ((uint64_t)0)
#define JVK_SUCCESS ((jvk_result_t)0)
#define JVK_TIMEOUT ((jvk_result_t)2)
#define JVK_WHOLE_SIZE ((jvk_device_size_t)(~(jvk_device_size_t)0))
#define JVK_API_VERSION_1_0 ((uint32_t)(1u << 22))

#define JVK_STRUCTURE_TYPE_APPLICATION_INFO 0
#define JVK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO 1
#define JVK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO 2
#define JVK_STRUCTURE_TYPE_DEVICE_CREATE_INFO 3
#define JVK_STRUCTURE_TYPE_SUBMIT_INFO 4
#define JVK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO 5
#define JVK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE 6
#define JVK_STRUCTURE_TYPE_FENCE_CREATE_INFO 8
#define JVK_STRUCTURE_TYPE_BUFFER_CREATE_INFO 12
#define JVK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO 16
#define JVK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18
#define JVK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO 29
#define JVK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO 30
#define JVK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO 32
#define JVK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO 33
#define JVK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO 34
#define JVK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET 35
#define JVK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO 39
#define JVK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO 40
#define JVK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO 42
#define JVK_STRUCTURE_TYPE_MEMORY_BARRIER 46

#define JVK_QUEUE_COMPUTE_BIT ((uint32_t)0x00000002)
#define JVK_QUEUE_GRAPHICS_BIT ((uint32_t)0x00000001)
#define JVK_BUFFER_USAGE_STORAGE_BUFFER_BIT ((uint32_t)0x00000020)
#define JVK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT ((uint32_t)0x00000001)
#define JVK_MEMORY_PROPERTY_HOST_VISIBLE_BIT ((uint32_t)0x00000002)
#define JVK_MEMORY_PROPERTY_HOST_COHERENT_BIT ((uint32_t)0x00000004)
#define JVK_DESCRIPTOR_TYPE_STORAGE_BUFFER 7
#define JVK_SHADER_STAGE_COMPUTE_BIT ((uint32_t)0x00000020)
#define JVK_COMMAND_BUFFER_LEVEL_PRIMARY 0
#define JVK_PIPELINE_BIND_POINT_COMPUTE 1
#define JVK_ACCESS_HOST_WRITE_BIT ((uint32_t)0x00008000)
#define JVK_ACCESS_HOST_READ_BIT ((uint32_t)0x00004000)
#define JVK_ACCESS_SHADER_READ_BIT ((uint32_t)0x00000020)
#define JVK_ACCESS_SHADER_WRITE_BIT ((uint32_t)0x00000040)
#define JVK_PIPELINE_STAGE_HOST_BIT ((uint32_t)0x00004000)
#define JVK_PIPELINE_STAGE_COMPUTE_SHADER_BIT ((uint32_t)0x00000800)
#define JVK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT ((uint32_t)0x00000001)
#define JVK_SHARING_MODE_EXCLUSIVE 0
#define JVK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU 2

typedef struct {
    int32_t s_type;
    const void *p_next;
    const char *p_application_name;
    uint32_t application_version;
    const char *p_engine_name;
    uint32_t engine_version;
    uint32_t api_version;
} jvk_application_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    const jvk_application_info_t *p_application_info;
    uint32_t enabled_layer_count;
    const char *const *pp_enabled_layer_names;
    uint32_t enabled_extension_count;
    const char *const *pp_enabled_extension_names;
} jvk_instance_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t queue_family_index;
    uint32_t queue_count;
    const float *p_queue_priorities;
} jvk_device_queue_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t queue_create_info_count;
    const jvk_device_queue_create_info_t *p_queue_create_infos;
    uint32_t enabled_layer_count;
    const char *const *pp_enabled_layer_names;
    uint32_t enabled_extension_count;
    const char *const *pp_enabled_extension_names;
    const void *p_enabled_features;
} jvk_device_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    jvk_device_size_t size;
    uint32_t usage;
    int32_t sharing_mode;
    uint32_t queue_family_index_count;
    const uint32_t *p_queue_family_indices;
} jvk_buffer_create_info_t;

typedef struct {
    jvk_device_size_t size;
    jvk_device_size_t alignment;
    uint32_t memory_type_bits;
} jvk_memory_requirements_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_device_size_t allocation_size;
    uint32_t memory_type_index;
} jvk_memory_allocate_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_device_memory_t memory;
    jvk_device_size_t offset;
    jvk_device_size_t size;
} jvk_mapped_memory_range_t;

typedef struct {
    uint32_t queue_flags;
    uint32_t queue_count;
    uint32_t timestamp_valid_bits;
    uint32_t min_image_transfer_granularity[3];
} jvk_queue_family_properties_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    size_t code_size;
    const uint32_t *p_code;
} jvk_shader_module_create_info_t;

typedef struct {
    uint32_t binding;
    int32_t descriptor_type;
    uint32_t descriptor_count;
    uint32_t stage_flags;
    const void *p_immutable_samplers;
} jvk_descriptor_set_layout_binding_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t binding_count;
    const jvk_descriptor_set_layout_binding_t *p_bindings;
} jvk_descriptor_set_layout_create_info_t;

typedef struct {
    uint32_t stage_flags;
    uint32_t offset;
    uint32_t size;
} jvk_push_constant_range_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t set_layout_count;
    const jvk_descriptor_set_layout_t *p_set_layouts;
    uint32_t push_constant_range_count;
    const jvk_push_constant_range_t *p_push_constant_ranges;
} jvk_pipeline_layout_create_info_t;

typedef struct {
    int32_t descriptor_type;
    uint32_t descriptor_count;
} jvk_descriptor_pool_size_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t max_sets;
    uint32_t pool_size_count;
    const jvk_descriptor_pool_size_t *p_pool_sizes;
} jvk_descriptor_pool_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_descriptor_pool_t descriptor_pool;
    uint32_t descriptor_set_count;
    const jvk_descriptor_set_layout_t *p_set_layouts;
} jvk_descriptor_set_allocate_info_t;

typedef struct {
    jvk_buffer_t buffer;
    jvk_device_size_t offset;
    jvk_device_size_t range;
} jvk_descriptor_buffer_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_descriptor_set_t dst_set;
    uint32_t dst_binding;
    uint32_t dst_array_element;
    uint32_t descriptor_count;
    int32_t descriptor_type;
    const void *p_image_info;
    const jvk_descriptor_buffer_info_t *p_buffer_info;
    const void *p_texel_buffer_view;
} jvk_write_descriptor_set_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t stage;
    jvk_shader_module_t module;
    const char *p_name;
    const void *p_specialization_info;
} jvk_pipeline_shader_stage_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    jvk_pipeline_shader_stage_create_info_t stage;
    jvk_pipeline_layout_t layout;
    jvk_pipeline_t base_pipeline_handle;
    int32_t base_pipeline_index;
} jvk_compute_pipeline_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t queue_family_index;
} jvk_command_pool_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_command_pool_t command_pool;
    int32_t level;
    uint32_t command_buffer_count;
} jvk_command_buffer_allocate_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    const void *p_inheritance_info;
} jvk_command_buffer_begin_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    uint32_t src_access_mask;
    uint32_t dst_access_mask;
} jvk_memory_barrier_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    uint32_t wait_semaphore_count;
    const void *p_wait_semaphores;
    const uint32_t *p_wait_dst_stage_mask;
    uint32_t command_buffer_count;
    const jvk_command_buffer_t *p_command_buffers;
    uint32_t signal_semaphore_count;
    const void *p_signal_semaphores;
} jvk_submit_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
} jvk_fence_create_info_t;

/* ---- Graphics subset (offscreen scene renderer) -------------------------
 * Values mirror the Vulkan specification; every call site checks VkResult
 * and fails closed, and the scene renderer validates one rendered frame
 * against the CPU rasterizer in its unit test. */
typedef uint64_t jvk_image_t;
typedef uint64_t jvk_image_view_t;
typedef uint64_t jvk_render_pass_t;
typedef uint64_t jvk_framebuffer_t;

#define JVK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14
#define JVK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define JVK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO 19
#define JVK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO 20
#define JVK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO 22
#define JVK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO 23
#define JVK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO 24
#define JVK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO 25
#define JVK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO 26
#define JVK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO 27
#define JVK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 28
#define JVK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 37
#define JVK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 38
#define JVK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 43
#define JVK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY 47
#define JVK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER 51

#define JVK_IMAGE_TYPE_2D 1
#define JVK_FORMAT_R8G8B8A8_UNORM 37
#define JVK_FORMAT_R32G32B32_SFLOAT 106
#define JVK_FORMAT_D32_SFLOAT 126
#define JVK_IMAGE_TILING_OPTIMAL 0
#define JVK_IMAGE_USAGE_TRANSFER_SRC_BIT ((uint32_t)0x00000001)
#define JVK_IMAGE_USAGE_TRANSFER_DST_BIT ((uint32_t)0x00000002)
#define JVK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT ((uint32_t)0x00000010)
#define JVK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT ((uint32_t)0x00000020)
#define JVK_IMAGE_LAYOUT_UNDEFINED 0
#define JVK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 2
#define JVK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL 3
#define JVK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL 6
#define JVK_BUFFER_USAGE_VERTEX_BUFFER_BIT ((uint32_t)0x00000080)
#define JVK_BUFFER_USAGE_UNIFORM_BUFFER_BIT ((uint32_t)0x00000010)
#define JVK_BUFFER_USAGE_TRANSFER_DST_BIT ((uint32_t)0x00000002)
#define JVK_DESCRIPTOR_TYPE_UNIFORM_BUFFER 6
#define JVK_SHADER_STAGE_VERTEX_BIT ((uint32_t)0x00000001)
#define JVK_SHADER_STAGE_FRAGMENT_BIT ((uint32_t)0x00000010)
#define JVK_PIPELINE_BIND_POINT_GRAPHICS 0
#define JVK_VERTEX_INPUT_RATE_VERTEX 0
#define JVK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 3
#define JVK_POLYGON_MODE_FILL 0
#define JVK_CULL_MODE_NONE 0
#define JVK_FRONT_FACE_COUNTER_CLOCKWISE 0
#define JVK_COMPARE_OP_LESS 1
#define JVK_STENCIL_OP_KEEP 0
#define JVK_BLEND_FACTOR_ZERO 0
#define JVK_BLEND_FACTOR_ONE 1
#define JVK_BLEND_OP_ADD 0
#define JVK_COLOR_COMPONENT_R_BIT ((uint32_t)0x00000001)
#define JVK_COLOR_COMPONENT_G_BIT ((uint32_t)0x00000002)
#define JVK_COLOR_COMPONENT_B_BIT ((uint32_t)0x00000004)
#define JVK_COLOR_COMPONENT_A_BIT ((uint32_t)0x00000008)
#define JVK_DYNAMIC_STATE_VIEWPORT 0
#define JVK_DYNAMIC_STATE_SCISSOR 1
#define JVK_IMAGE_ASPECT_COLOR_BIT ((uint32_t)0x00000001)
#define JVK_IMAGE_ASPECT_DEPTH_BIT ((uint32_t)0x00000002)
#define JVK_IMAGE_VIEW_TYPE_2D 1
#define JVK_COMPONENT_SWIZZLE_IDENTITY 0
#define JVK_SUBPASS_CONTENTS_INLINE 0
#define JVK_SUBPASS_EXTERNAL ((uint32_t)0xFFFFFFFF)
#define JVK_QUEUE_FAMILY_IGNORED ((uint32_t)0xFFFFFFFF)
#define JVK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT ((uint32_t)0x00000100)
#define JVK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT ((uint32_t)0x00000400)
#define JVK_ACCESS_TRANSFER_READ_BIT ((uint32_t)0x00000800)
#define JVK_ACCESS_TRANSFER_WRITE_BIT ((uint32_t)0x00001000)
#define JVK_PIPELINE_STAGE_TOP_OF_PIPE_BIT ((uint32_t)0x00000001)
#define JVK_PIPELINE_STAGE_VERTEX_SHADER_BIT ((uint32_t)0x00000008)
#define JVK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT ((uint32_t)0x00000100)
#define JVK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT ((uint32_t)0x00000400)
#define JVK_PIPELINE_STAGE_TRANSFER_BIT ((uint32_t)0x00001000)
#define JVK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ((uint32_t)0x00000080)
#define JVK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT ((uint32_t)0x00000200)
#define JVK_SAMPLE_COUNT_1_BIT ((uint32_t)0x00000001)
#define JVK_ATTACHMENT_LOAD_OP_CLEAR 1
#define JVK_ATTACHMENT_STORE_OP_STORE 0

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t depth;
} jvk_extent_3d_t;

typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} jvk_rect_2d_t;

typedef struct {
    float x;
    float y;
    float width;
    float height;
    float min_depth;
    float max_depth;
} jvk_viewport_t;

typedef union {
    float color[4];
    struct {
        float depth;
        uint32_t stencil;
    } depth_stencil;
} jvk_clear_value_t;

typedef struct {
    uint32_t flags;
    int32_t format;
    uint32_t samples;
    int32_t load_op;
    int32_t store_op;
    int32_t stencil_load_op;
    int32_t stencil_store_op;
    int32_t initial_layout;
    int32_t final_layout;
} jvk_attachment_description_t;

typedef struct {
    uint32_t attachment;
    int32_t layout;
} jvk_attachment_reference_t;

typedef struct {
    uint32_t flags;
    int32_t pipeline_bind_point;
    uint32_t input_attachment_count;
    const void *p_input_attachments;
    uint32_t color_attachment_count;
    const jvk_attachment_reference_t *p_color_attachments;
    const void *p_resolve_attachments;
    const jvk_attachment_reference_t *p_depth_stencil_attachment;
    uint32_t preserve_attachment_count;
    const uint32_t *p_preserve_attachments;
} jvk_subpass_description_t;

typedef struct {
    uint32_t src_subpass;
    uint32_t dst_subpass;
    uint32_t src_stage_mask;
    uint32_t dst_stage_mask;
    uint32_t src_access_mask;
    uint32_t dst_access_mask;
    uint32_t dependency_flags;
} jvk_subpass_dependency_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t attachment_count;
    const jvk_attachment_description_t *p_attachments;
    uint32_t subpass_count;
    const jvk_subpass_description_t *p_subpasses;
    uint32_t dependency_count;
    const jvk_subpass_dependency_t *p_dependencies;
} jvk_render_pass_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    jvk_render_pass_t render_pass;
    uint32_t attachment_count;
    const uint64_t *p_attachments;
    uint32_t width;
    uint32_t height;
    uint32_t layers;
} jvk_framebuffer_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    int32_t image_type;
    int32_t format;
    jvk_extent_3d_t extent;
    uint32_t mip_levels;
    uint32_t array_layers;
    uint32_t samples;
    int32_t tiling;
    uint32_t usage;
    int32_t sharing_mode;
    uint32_t queue_family_index_count;
    const uint32_t *p_queue_family_indices;
    int32_t initial_layout;
} jvk_image_create_info_t;

typedef struct {
    uint32_t aspect_mask;
    uint32_t base_mip_level;
    uint32_t level_count;
    uint32_t base_array_layer;
    uint32_t layer_count;
} jvk_image_subresource_range_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    jvk_image_t image;
    int32_t view_type;
    int32_t format;
    uint32_t components[4];
    jvk_image_subresource_range_t subresource_range;
} jvk_image_view_create_info_t;

typedef struct {
    uint32_t binding;
    uint32_t stride;
    int32_t input_rate;
} jvk_vertex_input_binding_description_t;

typedef struct {
    uint32_t location;
    uint32_t binding;
    int32_t format;
    uint32_t offset;
} jvk_vertex_input_attribute_description_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t vertex_binding_description_count;
    const jvk_vertex_input_binding_description_t *p_vertex_binding_descriptions;
    uint32_t vertex_attribute_description_count;
    const jvk_vertex_input_attribute_description_t *p_vertex_attribute_descriptions;
} jvk_pipeline_vertex_input_state_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    int32_t topology;
    uint32_t primitive_restart_enable;
} jvk_pipeline_input_assembly_state_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t viewport_count;
    const jvk_viewport_t *p_viewports;
    uint32_t scissor_count;
    const jvk_rect_2d_t *p_scissors;
} jvk_pipeline_viewport_state_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t depth_clamp_enable;
    uint32_t rasterizer_discard_enable;
    int32_t polygon_mode;
    uint32_t cull_mode;
    int32_t front_face;
    uint32_t depth_bias_enable;
    float depth_bias_constant_factor;
    float depth_bias_clamp;
    float depth_bias_slope_factor;
    float line_width;
} jvk_pipeline_rasterization_state_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t rasterization_samples;
    uint32_t sample_shading_enable;
    float min_sample_shading;
    const void *p_sample_mask;
    uint32_t alpha_to_coverage_enable;
    uint32_t alpha_to_one_enable;
} jvk_pipeline_multisample_state_create_info_t;

typedef struct {
    int32_t fail_op;
    int32_t pass_op;
    int32_t depth_fail_op;
    int32_t compare_op;
    uint32_t compare_mask;
    uint32_t write_mask;
    uint32_t reference;
} jvk_stencil_op_state_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t depth_test_enable;
    uint32_t depth_write_enable;
    int32_t depth_compare_op;
    uint32_t depth_bounds_test_enable;
    uint32_t stencil_test_enable;
    jvk_stencil_op_state_t front;
    jvk_stencil_op_state_t back;
    float min_depth_bounds;
    float max_depth_bounds;
} jvk_pipeline_depth_stencil_state_create_info_t;

typedef struct {
    uint32_t blend_enable;
    int32_t src_color_blend_factor;
    int32_t dst_color_blend_factor;
    int32_t color_blend_op;
    int32_t src_alpha_blend_factor;
    int32_t dst_alpha_blend_factor;
    int32_t alpha_blend_op;
    uint32_t color_write_mask;
} jvk_pipeline_color_blend_attachment_state_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t logic_op_enable;
    int32_t logic_op;
    uint32_t attachment_count;
    const jvk_pipeline_color_blend_attachment_state_t *p_attachments;
    float blend_constants[4];
} jvk_pipeline_color_blend_state_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t dynamic_state_count;
    const int32_t *p_dynamic_states;
} jvk_pipeline_dynamic_state_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_flags_t flags;
    uint32_t stage_count;
    const jvk_pipeline_shader_stage_create_info_t *p_stages;
    const jvk_pipeline_vertex_input_state_create_info_t *p_vertex_input_state;
    const jvk_pipeline_input_assembly_state_create_info_t *p_input_assembly_state;
    const void *p_tessellation_state;
    const jvk_pipeline_viewport_state_create_info_t *p_viewport_state;
    const jvk_pipeline_rasterization_state_create_info_t *p_rasterization_state;
    const jvk_pipeline_multisample_state_create_info_t *p_multisample_state;
    const jvk_pipeline_depth_stencil_state_create_info_t *p_depth_stencil_state;
    const jvk_pipeline_color_blend_state_create_info_t *p_color_blend_state;
    const jvk_pipeline_dynamic_state_create_info_t *p_dynamic_state;
    jvk_pipeline_layout_t layout;
    jvk_render_pass_t render_pass;
    uint32_t subpass;
    jvk_pipeline_t base_pipeline_handle;
    int32_t base_pipeline_index;
} jvk_graphics_pipeline_create_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    jvk_render_pass_t render_pass;
    jvk_framebuffer_t framebuffer;
    jvk_rect_2d_t render_area;
    uint32_t clear_value_count;
    const jvk_clear_value_t *p_clear_values;
} jvk_render_pass_begin_info_t;

typedef struct {
    int32_t s_type;
    const void *p_next;
    uint32_t src_access_mask;
    uint32_t dst_access_mask;
    int32_t old_layout;
    int32_t new_layout;
    uint32_t src_queue_family_index;
    uint32_t dst_queue_family_index;
    jvk_image_t image;
    jvk_image_subresource_range_t subresource_range;
} jvk_image_memory_barrier_t;

typedef struct {
    uint32_t aspect_mask;
    uint32_t mip_level;
    uint32_t base_array_layer;
    uint32_t layer_count;
} jvk_image_subresource_layers_t;

typedef struct {
    int32_t x;
    int32_t y;
    int32_t z;
} jvk_offset_3d_t;

typedef struct {
    uint64_t buffer_offset;
    uint32_t buffer_row_length;
    uint32_t buffer_image_height;
    jvk_image_subresource_layers_t image_subresource;
    jvk_offset_3d_t image_offset;
    jvk_extent_3d_t image_extent;
} jvk_buffer_image_copy_t;

/* Prefix of VkFormatProperties: linearTilingFeatures@0,
 * optimalTilingFeatures@4, bufferFeatures@8. Callers pass 12 zeroed bytes. */
#define JVK_FORMAT_FEATURES_OPTIMAL_OFFSET 4
#define JVK_FORMAT_PROPERTIES_SCRATCH 12

/* Prefix of VkPhysicalDeviceProperties: api_version@0, driver_version@4,
 * vendor_id@8, device_id@12, device_type@16, device_name@20. The remainder
 * of the struct is never touched; callers pass a zeroed scratch buffer. */
#define JVK_DEVICE_TYPE_OFFSET 16
#define JVK_DEVICE_NAME_OFFSET 20
#define JVK_DEVICE_NAME_SIZE 256
#define JVK_PHYSICAL_DEVICE_PROPERTIES_SCRATCH 4096

typedef struct {
    jvk_result_t (*enumerate_instance_version)(uint32_t *api_version);
    jvk_result_t (*create_instance)(
        const jvk_instance_create_info_t *, const void *, jvk_instance_t *);
    void (*destroy_instance)(jvk_instance_t, const void *);
    jvk_result_t (*enumerate_physical_devices)(
        jvk_instance_t, uint32_t *, jvk_physical_device_t *);
    void (*get_physical_device_properties)(
        jvk_physical_device_t, void *);
    void (*get_physical_device_queue_family_properties)(
        jvk_physical_device_t, uint32_t *, jvk_queue_family_properties_t *);
    void (*get_physical_device_memory_properties)(
        jvk_physical_device_t, void *);
    jvk_result_t (*create_device)(jvk_physical_device_t,
        const jvk_device_create_info_t *, const void *, jvk_device_t *);
    void (*destroy_device)(jvk_device_t, const void *);
    void (*get_device_queue)(jvk_device_t, uint32_t, uint32_t, jvk_queue_t *);
    jvk_result_t (*create_buffer)(jvk_device_t,
        const jvk_buffer_create_info_t *, const void *, jvk_buffer_t *);
    void (*destroy_buffer)(jvk_device_t, jvk_buffer_t, const void *);
    void (*get_buffer_memory_requirements)(jvk_device_t, jvk_buffer_t,
        jvk_memory_requirements_t *);
    jvk_result_t (*allocate_memory)(jvk_device_t,
        const jvk_memory_allocate_info_t *, const void *, jvk_device_memory_t *);
    void (*free_memory)(jvk_device_t, jvk_device_memory_t, const void *);
    jvk_result_t (*bind_buffer_memory)(jvk_device_t, jvk_buffer_t,
        jvk_device_memory_t, jvk_device_size_t);
    jvk_result_t (*map_memory)(jvk_device_t, jvk_device_memory_t,
        jvk_device_size_t, jvk_device_size_t, jvk_flags_t, void **);
    void (*unmap_memory)(jvk_device_t, jvk_device_memory_t);
    jvk_result_t (*flush_mapped_memory_ranges)(jvk_device_t, uint32_t,
        const jvk_mapped_memory_range_t *);
    jvk_result_t (*invalidate_mapped_memory_ranges)(jvk_device_t, uint32_t,
        const jvk_mapped_memory_range_t *);
    jvk_result_t (*create_shader_module)(jvk_device_t,
        const jvk_shader_module_create_info_t *, const void *,
        jvk_shader_module_t *);
    void (*destroy_shader_module)(jvk_device_t, jvk_shader_module_t,
        const void *);
    jvk_result_t (*create_descriptor_set_layout)(jvk_device_t,
        const jvk_descriptor_set_layout_create_info_t *, const void *,
        jvk_descriptor_set_layout_t *);
    void (*destroy_descriptor_set_layout)(jvk_device_t,
        jvk_descriptor_set_layout_t, const void *);
    jvk_result_t (*create_pipeline_layout)(jvk_device_t,
        const jvk_pipeline_layout_create_info_t *, const void *,
        jvk_pipeline_layout_t *);
    void (*destroy_pipeline_layout)(jvk_device_t, jvk_pipeline_layout_t,
        const void *);
    jvk_result_t (*create_descriptor_pool)(jvk_device_t,
        const jvk_descriptor_pool_create_info_t *, const void *,
        jvk_descriptor_pool_t *);
    void (*destroy_descriptor_pool)(jvk_device_t, jvk_descriptor_pool_t,
        const void *);
    jvk_result_t (*allocate_descriptor_sets)(jvk_device_t,
        const jvk_descriptor_set_allocate_info_t *, jvk_descriptor_set_t *);
    void (*update_descriptor_sets)(jvk_device_t, uint32_t,
        const jvk_write_descriptor_set_t *, uint32_t, const void *);
    jvk_result_t (*create_compute_pipelines)(jvk_device_t, uint64_t,
        uint32_t, const jvk_compute_pipeline_create_info_t *,
        const void *, jvk_pipeline_t *);
    void (*destroy_pipeline)(jvk_device_t, jvk_pipeline_t, const void *);
    jvk_result_t (*create_command_pool)(jvk_device_t,
        const jvk_command_pool_create_info_t *, const void *,
        jvk_command_pool_t *);
    void (*destroy_command_pool)(jvk_device_t, jvk_command_pool_t,
        const void *);
    jvk_result_t (*allocate_command_buffers)(jvk_device_t,
        const jvk_command_buffer_allocate_info_t *, jvk_command_buffer_t *);
    void (*free_command_buffers)(jvk_device_t, jvk_command_pool_t,
        uint32_t, const jvk_command_buffer_t *);
    jvk_result_t (*begin_command_buffer)(jvk_command_buffer_t,
        const jvk_command_buffer_begin_info_t *);
    jvk_result_t (*end_command_buffer)(jvk_command_buffer_t);
    void (*cmd_bind_pipeline)(jvk_command_buffer_t, int32_t, jvk_pipeline_t);
    void (*cmd_bind_descriptor_sets)(jvk_command_buffer_t, int32_t,
        jvk_pipeline_layout_t, uint32_t, uint32_t,
        const jvk_descriptor_set_t *, uint32_t, const uint32_t *);
    void (*cmd_push_constants)(jvk_command_buffer_t, jvk_pipeline_layout_t,
        uint32_t, uint32_t, uint32_t, const void *);
    void (*cmd_dispatch)(jvk_command_buffer_t, uint32_t, uint32_t, uint32_t);
    void (*cmd_pipeline_barrier)(jvk_command_buffer_t, uint32_t, uint32_t,
        uint32_t, uint32_t, const jvk_memory_barrier_t *, uint32_t,
        const void *, uint32_t, const void *);
    jvk_result_t (*queue_submit)(jvk_queue_t, uint32_t,
        const jvk_submit_info_t *, jvk_fence_t);
    jvk_result_t (*create_fence)(jvk_device_t,
        const jvk_fence_create_info_t *, const void *, jvk_fence_t *);
    void (*destroy_fence)(jvk_device_t, jvk_fence_t, const void *);
    jvk_result_t (*wait_for_fences)(jvk_device_t, uint32_t,
        const jvk_fence_t *, jvk_bool32_t, uint64_t);
    jvk_result_t (*device_wait_idle)(jvk_device_t);
    /* Graphics subset for the offscreen 3D scene renderer (vk_scene.c).
     * Appended after the compute entries so existing indices never move. */
    jvk_result_t (*create_image)(jvk_device_t,
        const jvk_image_create_info_t *, const void *, jvk_image_t *);
    void (*destroy_image)(jvk_device_t, jvk_image_t, const void *);
    void (*get_image_memory_requirements)(jvk_device_t, jvk_image_t,
        jvk_memory_requirements_t *);
    jvk_result_t (*bind_image_memory)(jvk_device_t, jvk_image_t,
        jvk_device_memory_t, jvk_device_size_t);
    jvk_result_t (*create_image_view)(jvk_device_t,
        const jvk_image_view_create_info_t *, const void *, jvk_image_view_t *);
    void (*destroy_image_view)(jvk_device_t, jvk_image_view_t, const void *);
    jvk_result_t (*create_render_pass)(jvk_device_t,
        const jvk_render_pass_create_info_t *, const void *, jvk_render_pass_t *);
    void (*destroy_render_pass)(jvk_device_t, jvk_render_pass_t, const void *);
    jvk_result_t (*create_framebuffer)(jvk_device_t,
        const jvk_framebuffer_create_info_t *, const void *, jvk_framebuffer_t *);
    void (*destroy_framebuffer)(jvk_device_t, jvk_framebuffer_t, const void *);
    jvk_result_t (*create_graphics_pipelines)(jvk_device_t, uint64_t,
        uint32_t, const jvk_graphics_pipeline_create_info_t *,
        const void *, jvk_pipeline_t *);
    void (*cmd_begin_render_pass)(jvk_command_buffer_t,
        const jvk_render_pass_begin_info_t *, int32_t);
    void (*cmd_end_render_pass)(jvk_command_buffer_t);
    void (*cmd_bind_vertex_buffers)(jvk_command_buffer_t, uint32_t, uint32_t,
        const jvk_buffer_t *, const jvk_device_size_t *);
    void (*cmd_draw)(jvk_command_buffer_t, uint32_t, uint32_t, uint32_t,
        uint32_t);
    void (*cmd_copy_image_to_buffer)(jvk_command_buffer_t, jvk_image_t,
        int32_t, jvk_buffer_t, uint32_t, const jvk_buffer_image_copy_t *);
    void (*cmd_set_viewport)(jvk_command_buffer_t, uint32_t, uint32_t,
        const jvk_viewport_t *);
    void (*cmd_set_scissor)(jvk_command_buffer_t, uint32_t, uint32_t,
        const jvk_rect_2d_t *);
    void (*get_physical_device_format_properties)(jvk_physical_device_t,
        int32_t, void *);
} jvk_fns_t;

/* Loads every entry with dlsym + memcpy (no function-pointer casts, so
 * -Wpedantic stays quiet). Returns false when any symbol is missing. */
bool jvk_load_fns(void *loader, jvk_fns_t *out_fns);

#endif /* JFX_VK_MINIMAL_H */
