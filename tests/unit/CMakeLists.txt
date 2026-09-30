include("${CMAKE_CURRENT_SOURCE_DIR}/TestHelpers.cmake")

jfx_test_target(test_tillyz SOURCES tilly/test_tillyz.c LIBRARIES tillyz)

# The engine, its memory/scheduler/event subsystems and the resource objects.
jfx_test_target(test_core SOURCES core/test_engine.c LIBRARIES jfx_core)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_library(test_plugin_module MODULE core/test_plugin_module.c)
target_include_directories(test_plugin_module PRIVATE ${PROJECT_SOURCE_DIR}/core/include)
if(UNIX AND NOT APPLE)
    target_link_options(test_plugin_module PRIVATE -Wl,--allow-shlib-undefined)
endif()
set_project_warnings(test_plugin_module)

jfx_test_target(test_plugin_host SOURCES core/test_plugin_host.c LIBRARIES jfx_core)
target_include_directories(test_plugin_host PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(test_plugin_host PROPERTIES ENABLE_EXPORTS ON)

jfx_test_target(test_containers SOURCES containers/test_containers.c LIBRARIES tilly)

jfx_test_target(test_joltscript SOURCES joltscript/test_vm.c LIBRARIES joltscript_glue)
jfx_test_target(test_pipeline SOURCES joltscript/test_pipeline.c LIBRARIES joltscript_glue)
jfx_test_target(test_compiler SOURCES ${CMAKE_SOURCE_DIR}/joltscript/tests/unit/test_compiler.c LIBRARIES joltscript_glue joltscript_execution)
target_include_directories(test_compiler PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(test_compiler PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
jfx_test_target(test_vm SOURCES ${CMAKE_SOURCE_DIR}/joltscript/tests/unit/test_vm.c LIBRARIES joltscript_glue joltscript_execution)
target_include_directories(test_vm PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(test_vm PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
jfx_test_target(test_integration SOURCES ${CMAKE_SOURCE_DIR}/joltscript/tests/integration/test_integration.c LIBRARIES joltscript_glue joltscript_execution)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(test_integration PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
jfx_test_target(test_conformance SOURCES ${CMAKE_SOURCE_DIR}/joltscript/tests/conformance/test_conformance.c LIBRARIES joltscript_glue joltscript_execution)
target_include_directories(test_conformance PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(test_conformance PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)
jfx_test_target(test_benchmarks SOURCES ${CMAKE_SOURCE_DIR}/joltscript/tests/benchmarks/test_benchmarks.c LIBRARIES joltscript_glue joltscript_execution)
target_include_directories(test_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/glue/include)
target_include_directories(test_benchmarks PRIVATE ${CMAKE_SOURCE_DIR}/joltscript/layers/execution/include)

# The bundled effect catalog, and the custom target that runs it by hand.
jfx_test_target(test_effects SOURCES ../kernels/test_effects.c LIBRARIES jolt_effects)
add_custom_target(validate_kernels COMMAND $<TARGET_FILE:test_effects> DEPENDS test_effects)

# The reference GPU backend, exercised against a real driver when one exists.
if(JFX_BACKEND_VULKAN)
    jfx_test_target(test_vulkan_backend SOURCES backends/test_vulkan_backend.c
        LIBRARIES jfx_backend_vulkan)
endif()

# The portable CPU-path adapters. One conformance run covers whichever built.
set(JFX_FALLBACK_BACKEND_TARGETS "")
foreach(backend IN ITEMS metal d3d12 webgpu)
    if(JFX_BACKEND_${backend})
        list(APPEND JFX_FALLBACK_BACKEND_TARGETS jfx_backend_${backend})
    endif()
endforeach()
if(JFX_FALLBACK_BACKEND_TARGETS)
    jfx_test_target(test_fallback_backends SOURCES backends/test_fallback_backends.c
        LIBRARIES jfx_backend_software ${JFX_FALLBACK_BACKEND_TARGETS})
    target_compile_definitions(test_fallback_backends PRIVATE
        JFX_TEST_BACKEND_METAL=$<BOOL:${JFX_BACKEND_METAL}>
        JFX_TEST_BACKEND_D3D12=$<BOOL:${JFX_BACKEND_D3D12}>
        JFX_TEST_BACKEND_WEBGPU=$<BOOL:${JFX_BACKEND_WEBGPU}>
    )
endif()

if(JFX_EXT_LUA AND JFX_EXT_MRUBY)
    jfx_test_target(test_extensions SOURCES extif/test_extensions.c
        LIBRARIES jfx_lua jfx_mruby)
    target_include_directories(test_extensions PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(JFX_FRONTEND_DESKTOP)
    jfx_test_target(test_desktop_frontend SOURCES frontends/test_desktop_frontend.cpp
        LIBRARIES jfx_desktop_frontend)
    target_include_directories(test_desktop_frontend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(JFX_FRONTEND_MOBILE)
    jfx_test_target(test_mobile_frontend SOURCES frontends/test_mobile_frontend.c
        LIBRARIES jfx_mobile_player)
    target_include_directories(test_mobile_frontend PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

if(JFX_PLUGIN_HOST_BRIDGES)
    jfx_test_target(test_host_plugins SOURCES frontends/test_host_plugins.c
        LIBRARIES jfx_ae_plugin jfx_premiere_plugin jfx_davinci_plugin)
endif()

if(JFX_FRONTEND_WEB)
    jfx_test_target(test_web_session SOURCES frontends/test_web_session.c
        LIBRARIES jfx_web_session)
    target_include_directories(test_web_session PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# The colour LUT library: container, every supported on-disk format, the
# writers, and the samplers.
jfx_test_target(test_lut SOURCES color/test_lut.c LIBRARIES jfx_core)

# The node graph: typing, cycle rejection, and the grading/compositing maths.
jfx_test_target(test_compose SOURCES color/test_compose.c LIBRARIES jfx_core)

# The NLE model: tracks, clips, the per-clip effect stack and keyframes.
jfx_test_target(test_timeline SOURCES color/test_timeline.c LIBRARIES jfx_core)

# The text project format every frontend reads and writes.
jfx_test_target(test_project SOURCES color/test_project.c LIBRARIES jfx_core)

# The shared frontend contract, exercised with a deliberately minimal frontend
# so the NOT_IMPLEMENTED paths are covered.
jfx_test_target(test_frontend_contract SOURCES frontends/test_frontend_contract.c
    LIBRARIES jfx_frontend_common)

# ---- Registration ---------------------------------------------------------
#
# Every test is registered here, next to the targets it needs, so a target and
# its add_test() cannot drift apart. Each carries a label so `ctest -L unit`,
# `-L conformance` and `-LE frontend` select meaningful subsets, plus a timeout
# so a hung worker thread or a blocked display server fails the run instead of
# stalling it.
function(jfx_register_test name)
    add_test(NAME ${name} COMMAND ${ARGN})
    set_property(TEST ${name} APPEND PROPERTY LABELS unit)
    set_property(TEST ${name} PROPERTY TIMEOUT 120)
endfunction()

jfx_register_test(test_tillyz test_tillyz)
jfx_register_test(test_core test_core)
jfx_register_test(plugin_host test_plugin_host $<TARGET_FILE:test_plugin_module>)
jfx_register_test(test_containers test_containers)
jfx_register_test(test_joltscript test_joltscript)
jfx_register_test(test_pipeline test_pipeline)
jfx_register_test(test_compiler test_compiler)
jfx_register_test(test_vm test_vm)
jfx_register_test(test_integration test_integration)
jfx_register_test(test_conformance test_conformance)
jfx_register_test(test_benchmarks test_benchmarks)
jfx_register_test(kernel_color_conformance test_effects)
set_property(TEST kernel_color_conformance APPEND PROPERTY LABELS conformance)

if(JFX_BACKEND_VULKAN)
    jfx_register_test(test_vulkan_backend test_vulkan_backend)
endif()
if(JFX_FALLBACK_BACKEND_TARGETS)
    jfx_register_test(backend_fallback_conformance test_fallback_backends)
    set_property(TEST backend_fallback_conformance APPEND PROPERTY LABELS conformance)
endif()
if(JFX_EXT_LUA AND JFX_EXT_MRUBY)
    jfx_register_test(ext_conformance test_extensions)
    set_property(TEST ext_conformance APPEND PROPERTY LABELS conformance)
endif()
if(JFX_FRONTEND_DESKTOP)
    jfx_register_test(desktop_frontend test_desktop_frontend)
    set_property(TEST desktop_frontend APPEND PROPERTY LABELS frontend)
endif()
if(JFX_FRONTEND_MOBILE)
    jfx_register_test(mobile_frontend test_mobile_frontend)
    set_property(TEST mobile_frontend APPEND PROPERTY LABELS frontend)
endif()
if(JFX_PLUGIN_HOST_BRIDGES)
    jfx_register_test(host_plugin_contract test_host_plugins)
    set_property(TEST host_plugin_contract APPEND PROPERTY LABELS conformance)
endif()
if(JFX_FRONTEND_WEB)
    jfx_register_test(web_session test_web_session)
    set_property(TEST web_session APPEND PROPERTY LABELS frontend)
endif()
jfx_register_test(lut test_lut)
jfx_register_test(compose test_compose)
jfx_register_test(timeline test_timeline)
jfx_register_test(project test_project)
jfx_register_test(frontend_contract test_frontend_contract)
set_property(TEST frontend_contract APPEND PROPERTY LABELS conformance)

jfx_test_target(test_editor SOURCES color/test_editor.c LIBRARIES jfx_core)
jfx_register_test(editor test_editor)

jfx_test_target(test_image_kernels SOURCES ../kernels/test_image_kernels.c LIBRARIES jolt_effects)
jfx_register_test(kernel_image_batch test_image_kernels)
jfx_test_target(test_image_program SOURCES ../kernels/test_image_program.c LIBRARIES joltscript_glue)
jfx_register_test(kernel_image_program test_image_program)
target_compile_definitions(test_image_kernels PRIVATE JOLT_TEST_ROOT="${PROJECT_SOURCE_DIR}")

# Verify every new source using the shipping compiler driver as well.
foreach(image_kernel IN ITEMS
    transform/translate2d transform/scale2d transform/rotate2d transform/skew2d
    transform/transform3d transform/perspective_warp transform/affine_transform
    transform/polar_transform color/brightness_contrast color/hue_rotate
    color/color_balance color/levels color/curves color/lut_apply
    color/channel_mixer color/vibrance color/color_temperature
    blur_sharpen/gaussian_blur blur_sharpen/motion_blur blur_sharpen/radial_blur
    blur_sharpen/box_blur blur_sharpen/unsharp_mask blur_sharpen/bilateral_filter
    blur_sharpen/lens_blur blur_sharpen/tilt_shift
    distortion/wave_distort distortion/ripple distortion/bulge_pinch
    distortion/lens_distortion distortion/displacement_map
    distortion/turbulence_warp distortion/polar_coords distortion/twist
    generative/solid_color generative/gradient_linear generative/gradient_radial
    generative/checkerboard generative/grid generative/plasma
    generative/cell_noise generative/voronoi generative/mandelbrot
    noise/perlin_noise noise/simplex_noise noise/worley_noise
    noise/fractal_noise noise/white_noise
    utility/passthrough utility/premultiply_alpha utility/format_convert
    utility/channel_split utility/clamp_values utility/remap_range
    fractal/julia_set fractal/barnsley_fern
    light/bloom light/glow
    video_effects/video_noise_grain video_effects/video_pixelate
    keying/luma_keyer keying/keying_chroma_keyer
    compositing/blend compositing/alpha_composite compositing/screen
    compositing/multiply compositing/overlay
    compositing/chroma_key compositing/luma_matte compositing/difference
    compositing/add
    color_calibration/calib_white_balance color_calibration/calib_hdr_tone_map
    color_calibration/calib_gamma_curve color_calibration/calib_black_level
    color_calibration/calib_white_point
    color_grading/grade_teal_orange color_grading/grade_black_and_white
    color_grading/grade_split_toning color_grading/grade_duotone
    color_grading/grade_sepia
    stylize/stylize_cartoon stylize/stylize_pixel_art stylize/stylize_neon
    stylize/stylize_thermal stylize/stylize_night_vision
    stylize/stylize_watercolor stylize/stylize_pencil_sketch
    stylize/stylize_oil_paint
    transition/cross_dissolve transition/wipe
    transition/zoom_transition transition/slide_transition
    transition/luma_wipe
    mask_matte/rect_mask mask_matte/ellipse_mask mask_matte/feather_mask
    mask_matte/track_matte mask_matte/bezier_mask
    time/frame_delay time/time_remap time/motion_trail
    time/frame_blend time/freeze_frame
    light/point_light light/lens_flare light/shadow_cast
    light/directional_light
    fractal/ifs_fractal
    geometry/stroke_path geometry/boolean_op geometry/offset_path
    geometry/fill_shape
    data_analysis/motion_detect data_analysis/histogram_compute
    data_analysis/edge_detect
    video_effects/video_strobe
    utility/channel_merge)
    string(REPLACE "/" "_" test_name "${image_kernel}")
    jfx_register_test(kernel_verify_${test_name} joltc --check
        --image-library "${PROJECT_SOURCE_DIR}/kernels/common/image.jolt"
        "${PROJECT_SOURCE_DIR}/kernels/${image_kernel}.jolt")
endforeach()
add_dependencies(test_image_kernels joltc)
add_dependencies(validate_kernels test_image_kernels test_image_program)
add_custom_command(TARGET validate_kernels POST_BUILD
    COMMAND $<TARGET_FILE:test_image_kernels>
    COMMAND $<TARGET_FILE:test_image_program>)
