include("${CMAKE_CURRENT_SOURCE_DIR}/TestHelpers.cmake")

jfx_test_target(test_tillyz SOURCES tilly/test_tillyz.c LIBRARIES tillyz)
jfx_test_target(test_allocator SOURCES tilly/test_allocator.c LIBRARIES tilly)
jfx_test_target(test_memory SOURCES tilly/test_memory.cpp LIBRARIES tilly)
add_test(NAME memtkx_memory COMMAND test_memory)
jfx_test_target(test_image SOURCES core/test_image.c LIBRARIES jfx_core)
add_test(NAME image_memory COMMAND test_image)
if(UNIX AND NOT EMSCRIPTEN)
    add_library(tilly_test_module MODULE tilly/module_fixture.c)
    add_library(tilly_test_dependency MODULE tilly/module_fixture.c)
    target_include_directories(tilly_test_module PRIVATE ${PROJECT_SOURCE_DIR}/tilly/include)
    target_include_directories(tilly_test_dependency PRIVATE ${PROJECT_SOURCE_DIR}/tilly/include)
    if(APPLE)
        target_link_options(tilly_test_module PRIVATE -undefined dynamic_lookup)
        target_link_options(tilly_test_dependency PRIVATE -undefined dynamic_lookup)
    endif()
    set_project_warnings(tilly_test_module)
    set_project_warnings(tilly_test_dependency)
    jfx_test_target(test_tilly_module SOURCES tilly/test_module.c LIBRARIES tilly)
    set_target_properties(test_tilly_module PROPERTIES ENABLE_EXPORTS ON)
    add_dependencies(test_tilly_module tilly_test_module tilly_test_dependency)
endif()

# The engine, its memory/scheduler/event subsystems and the resource objects.
jfx_test_target(test_core SOURCES core/test_engine.c LIBRARIES jfx_core)
target_include_directories(test_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
jfx_test_target(test_animation SOURCES core/test_animation.cpp LIBRARIES jfx_core)
jfx_test_target(test_scheduler SOURCES core/test_scheduler.c LIBRARIES jfx_core)

add_library(test_plugin_module MODULE core/test_plugin_module.c)
target_include_directories(test_plugin_module PRIVATE ${PROJECT_SOURCE_DIR}/src/include)
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
jfx_test_target(test_ext_bridge SOURCES ${PROJECT_SOURCE_DIR}/extif/tests/conformance/test_bridge.c
    LIBRARIES jfx_extif)
jfx_test_target(test_ext_resources SOURCES ${PROJECT_SOURCE_DIR}/extif/tests/conformance/test_resources.c
    LIBRARIES jfx_extif)
if(JFX_EXT_WASM)
    jfx_test_target(test_ext_wasm SOURCES ${PROJECT_SOURCE_DIR}/extif/tests/conformance/test_wasm.c
        LIBRARIES jfx_extif)
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
jfx_register_test(test_animation test_animation)
jfx_register_test(test_scheduler test_scheduler)
jfx_register_test(plugin_host test_plugin_host $<TARGET_FILE:test_plugin_module>)
if(NOT EMSCRIPTEN)
    if(NOT TARGET jfx_example_plugin)
        jfx_add_plugin(jfx_example_plugin SOURCES "${PROJECT_SOURCE_DIR}/sdk/examples/tint/tint.c")
    endif()
    jfx_test_target(test_plugin_sdk SOURCES core/test_plugin_sdk.c LIBRARIES jfx_core)
    target_include_directories(test_plugin_sdk PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_dependencies(test_plugin_sdk jfx_example_plugin)
    jfx_register_test(plugin_sdk test_plugin_sdk $<TARGET_FILE:jfx_example_plugin> "${CMAKE_CURRENT_BINARY_DIR}/plugin-export.mkv")
    set_property(TEST plugin_sdk APPEND PROPERTY LABELS conformance)
endif()
jfx_register_test(test_containers test_containers)
jfx_register_test(allocator test_allocator)
if(TARGET test_tilly_module)
    jfx_register_test(tilly_module test_tilly_module $<TARGET_FILE:tilly_test_module> $<TARGET_FILE:tilly_test_dependency>)
    jfx_register_test(tilly_module_callbacks test_tilly_module $<TARGET_FILE:tilly_test_module> $<TARGET_FILE:tilly_test_dependency> callbacks)
endif()

# The OFX host adapter is tested against a real plugin binary staged into a real
# bundle layout, so the dynamic-loading path is exercised rather than stubbed.
# The test assembles the bundle itself, so it needs a scratch directory.
if(JFX_PLUGIN_OPENFX AND NOT EMSCRIPTEN)
    jfx_test_target(test_ofx_host SOURCES frontends/test_ofx_host.c
        LIBRARIES jfx_ofx_host)
    add_dependencies(test_ofx_host jfx_ofx_test_plugin)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/ofx-bundles")
    jfx_register_test(ofx_host test_ofx_host
        "${CMAKE_CURRENT_BINARY_DIR}/ofx-bundles"
        "$<TARGET_FILE:jfx_ofx_test_plugin>")
    set_property(TEST ofx_host APPEND PROPERTY LABELS conformance)
endif()
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
jfx_register_test(ext_conformance_bridge test_ext_bridge)
set_property(TEST ext_conformance_bridge APPEND PROPERTY LABELS conformance)
jfx_register_test(ext_conformance_resources test_ext_resources)
set_property(TEST ext_conformance_resources APPEND PROPERTY LABELS conformance)
if(JFX_EXT_WASM)
    jfx_register_test(ext_conformance_wasm test_ext_wasm)
    set_property(TEST ext_conformance_wasm APPEND PROPERTY LABELS conformance)
endif()
if(JFX_FRONTEND_DESKTOP)
    jfx_register_test(desktop_frontend test_desktop_frontend)
    set_property(TEST desktop_frontend APPEND PROPERTY LABELS frontend)
    jfx_test_target(test_composition_ui SOURCES frontends/test_composition_ui.cpp LIBRARIES jfx_desktop_frontend)
    target_include_directories(test_composition_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/third_party/imgui)
    jfx_register_test(composition_ui_tests test_composition_ui)
    set_property(TEST composition_ui_tests APPEND PROPERTY LABELS "frontend;conformance")
    jfx_test_target(test_animation_ui SOURCES frontends/test_animation_ui.cpp LIBRARIES jfx_desktop_frontend)
    target_include_directories(test_animation_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(test_animation_ui SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/imgui)
    jfx_register_test(animation_ui_tests test_animation_ui)
    set_property(TEST animation_ui_tests APPEND PROPERTY LABELS "frontend;conformance")
    if(TARGET jfx_example_plugin)
        jfx_test_target(test_workspace_ui SOURCES frontends/test_workspace_ui.cpp LIBRARIES jfx_desktop_frontend)
        target_include_directories(test_workspace_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_include_directories(test_workspace_ui SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/imgui)
        add_dependencies(test_workspace_ui jfx_example_plugin)
        jfx_register_test(workspace_ui_tests test_workspace_ui $<TARGET_FILE:jfx_example_plugin>)
        set_property(TEST workspace_ui_tests APPEND PROPERTY LABELS "frontend;conformance")
    endif()
endif()
if(JFX_FRONTEND_MOBILE)
    jfx_register_test(mobile_frontend test_mobile_frontend)
    set_property(TEST mobile_frontend APPEND PROPERTY LABELS frontend)
endif()
if(JFX_PLUGIN_HOST_BRIDGES)
    jfx_register_test(host_plugin_contract test_host_plugins "${CMAKE_CURRENT_BINARY_DIR}/host-nle-frame.ppm")
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
jfx_test_target(test_nle SOURCES color/test_nle.c LIBRARIES jfx_core)
jfx_register_test(nle test_nle)
jfx_test_target(test_audio SOURCES color/test_audio.c LIBRARIES jfx_core)
jfx_register_test(audio_mixing test_audio "${CMAKE_CURRENT_BINARY_DIR}/audio-fixture.wav")
jfx_test_target(test_daw_features SOURCES color/test_daw_features.c LIBRARIES jfx_core)
jfx_register_test(daw_notes_state_recording test_daw_features "${CMAKE_CURRENT_BINARY_DIR}/recording-fixture.wav")
if(JFX_AUDIO_VST3)
    add_library(test_vst3_plugin MODULE color/vst3_fixture.cpp
        "${JFX_VST3_INCLUDE_DIR}/pluginterfaces/base/funknown.cpp"
        "${JFX_VST3_INCLUDE_DIR}/pluginterfaces/base/coreiids.cpp")
    target_include_directories(test_vst3_plugin SYSTEM PRIVATE "${JFX_VST3_INCLUDE_DIR}")
    set_target_properties(test_vst3_plugin PROPERTIES PREFIX "" SUFFIX ".vst3")
    if(APPLE)
        set_target_properties(test_vst3_plugin PROPERTIES BUNDLE TRUE BUNDLE_EXTENSION vst3)
        set(JFX_TEST_VST3_PATH "$<TARGET_BUNDLE_DIR:test_vst3_plugin>")
    else()
        set(JFX_TEST_VST3_PATH "$<TARGET_FILE:test_vst3_plugin>")
    endif()
    jfx_test_target(test_vst3 SOURCES color/test_vst3.c LIBRARIES jfx_core)
    add_dependencies(test_vst3 test_vst3_plugin)
    jfx_register_test(vst3_audio_host test_vst3 ${JFX_TEST_VST3_PATH} "${CMAKE_CURRENT_BINARY_DIR}/vst3-audio.wav")
    if(JFX_FRONTEND_DESKTOP)
        jfx_test_target(test_daw_ui SOURCES frontends/test_daw_ui.cpp LIBRARIES jfx_desktop_frontend)
        target_include_directories(test_daw_ui PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_include_directories(test_daw_ui SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/imgui)
        add_dependencies(test_daw_ui test_vst3_plugin)
        jfx_register_test(daw_ui_tests test_daw_ui ${JFX_TEST_VST3_PATH} "${CMAKE_CURRENT_BINARY_DIR}/daw-ui.wav")
        get_property(daw_window_enabled GLOBAL PROPERTY JFX_DESKTOP_WINDOW_ENABLED)
        if(daw_window_enabled)
            jfx_test_target(test_daw_devices SOURCES frontends/test_daw_devices.cpp LIBRARIES jfx_desktop_frontend)
            target_include_directories(test_daw_devices PRIVATE ${PROJECT_SOURCE_DIR}/frontends/desktop/src)
            target_include_directories(test_daw_devices SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/imgui)
            add_dependencies(test_daw_devices test_vst3_plugin)
            jfx_register_test(daw_native_window_capture test_daw_devices ${JFX_TEST_VST3_PATH} "${CMAKE_CURRENT_BINARY_DIR}/device-take.wav")
            set_tests_properties(daw_native_window_capture PROPERTIES ENVIRONMENT "SDL_AUDIODRIVER=dummy" SKIP_RETURN_CODE 77 LABELS "unit;frontend;window")
        endif()
        set_property(TEST daw_ui_tests APPEND PROPERTY LABELS "frontend;conformance")
    endif()
else()
    jfx_test_target(test_vst3 SOURCES color/test_vst3.c LIBRARIES jfx_core)
    jfx_register_test(vst3_audio_model test_vst3)
endif()
jfx_test_target(test_audio_task SOURCES joltscript/test_audio_task.c LIBRARIES jolt_effects)
jfx_register_test(audio_task test_audio_task "${PROJECT_SOURCE_DIR}/tests/kernels/audio_mix.test.jolt")
jfx_register_test(kernel_verify_audio_mix joltc --check "${PROJECT_SOURCE_DIR}/kernels/audio_reactive/audio_mix.jolt")
add_dependencies(validate_kernels test_audio_task)
jfx_test_target(test_media_export SOURCES color/test_media_export.c LIBRARIES jfx_core)
jfx_register_test(media_export test_media_export "${CMAKE_CURRENT_BINARY_DIR}/media-export.mkv")
jfx_test_target(test_composition_editor SOURCES color/test_composition_editor.c LIBRARIES jfx_core)
jfx_register_test(composition_editor test_composition_editor "${CMAKE_CURRENT_BINARY_DIR}/composition-editor-frame.ppm")
jfx_test_target(test_color_grading SOURCES color/test_color_grading.c LIBRARIES jfx_core)
jfx_register_test(color_grading test_color_grading)
target_compile_definitions(test_color_grading PRIVATE JOLT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
jfx_test_target(test_image_task SOURCES joltscript/test_image_task.c LIBRARIES joltscript_execution)
jfx_register_test(image_task test_image_task)
if(JFX_FRONTEND_DESKTOP AND JFX_FRONTEND_MOBILE AND JFX_FRONTEND_WEB)
    jfx_test_target(test_color_frontends SOURCES frontends/test_color_frontends.cpp
        LIBRARIES jfx_desktop_frontend jfx_mobile_player jfx_web_session)
    target_include_directories(test_color_frontends PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_color_frontends PRIVATE JOLT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
    jfx_register_test(color_frontend_conformance test_color_frontends)
    set_property(TEST color_frontend_conformance APPEND PROPERTY LABELS "frontend;conformance")
    jfx_test_target(test_nle_frontends SOURCES frontends/test_nle_frontends.cpp
        LIBRARIES jfx_desktop_frontend jfx_mobile_player jfx_web_session)
    target_include_directories(test_nle_frontends PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_nle_frontends PRIVATE JOLT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
    jfx_register_test(nle_frontend_conformance test_nle_frontends "${CMAKE_CURRENT_BINARY_DIR}/nle-frame.ppm")
    set_property(TEST nle_frontend_conformance APPEND PROPERTY LABELS "frontend;conformance")
    jfx_test_target(test_composition_frontends SOURCES frontends/test_composition_frontends.cpp
        LIBRARIES jfx_desktop_frontend jfx_mobile_player jfx_web_session)
    target_include_directories(test_composition_frontends PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_compile_definitions(test_composition_frontends PRIVATE JOLT_TEST_ROOT="${PROJECT_SOURCE_DIR}")
    if(JFX_PLUGIN_HOST_BRIDGES)
        target_link_libraries(test_composition_frontends PRIVATE jfx_host_plugin_common)
        target_compile_definitions(test_composition_frontends PRIVATE JFX_TEST_HOST_COMPOSITION=1)
    endif()
    jfx_register_test(composition_frontend_conformance test_composition_frontends "${CMAKE_CURRENT_BINARY_DIR}/composition-frame.ppm")
    set_property(TEST composition_frontend_conformance APPEND PROPERTY LABELS "frontend;conformance")
    if(JFX_PLUGIN_HOST_BRIDGES)
        jfx_test_target(test_media_frontends SOURCES frontends/test_media_frontends.cpp
            LIBRARIES jfx_desktop_frontend jfx_mobile_player jfx_web_session jfx_host_plugin_common)
        target_include_directories(test_media_frontends PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        jfx_register_test(media_frontend_conformance test_media_frontends
            "${CMAKE_CURRENT_BINARY_DIR}/frontend-audio.wav" "${CMAKE_CURRENT_BINARY_DIR}/frontend-video.mkv")
        set_property(TEST media_frontend_conformance APPEND PROPERTY LABELS "frontend;conformance")
    endif()
endif()

jfx_test_target(test_image_kernels SOURCES ../kernels/test_image_kernels.c LIBRARIES jolt_effects)
jfx_register_test(kernel_image_batch test_image_kernels)
jfx_test_target(test_image_program SOURCES ../kernels/test_image_program.c LIBRARIES joltscript_glue)
jfx_register_test(kernel_image_program test_image_program)
target_compile_definitions(test_image_kernels PRIVATE JOLT_TEST_ROOT="${PROJECT_SOURCE_DIR}")

# Verify every new source using the shipping compiler driver as well.
foreach(image_kernel IN ITEMS
    color_grading/grade_primary color_grading/grade_lut color_calibration/calib_lut
    transform/translate2d transform/scale2d transform/rotate2d transform/skew2d
    transform/transform3d transform/perspective_warp transform/affine_transform
    transform/polar_transform color/brightness_contrast color/hue_rotate
    color/color_balance color/levels color/curves color/lut_apply
    color/channel_mixer color/vibrance color/color_temperature
    blur_sharpen/gaussian_blur blur_sharpen/motion_blur blur_sharpen/radial_blur
    blur_sharpen/box_blur blur_sharpen/unsharp_mask blur_sharpen/bilateral_filter
    blur_sharpen/lens_blur blur_sharpen/tilt_shift distortion/wave_distort
    distortion/ripple distortion/bulge_pinch distortion/lens_distortion
    distortion/displacement_map distortion/turbulence_warp
    distortion/polar_coords distortion/twist generative/solid_color
    generative/gradient_linear generative/gradient_radial generative/checkerboard
    generative/grid generative/plasma generative/cell_noise generative/voronoi
    generative/mandelbrot noise/perlin_noise noise/simplex_noise
    noise/worley_noise noise/fractal_noise noise/white_noise utility/passthrough
    utility/premultiply_alpha utility/format_convert utility/channel_split
    utility/clamp_values utility/remap_range utility/channel_merge
    fractal/julia_set fractal/barnsley_fern fractal/ifs_fractal light/bloom
    light/glow light/point_light light/lens_flare light/shadow_cast
    light/directional_light video_effects/video_noise_grain
    video_effects/video_pixelate video_effects/video_strobe
    video_effects/video_flicker video_effects/video_mosaic
    video_effects/video_ghosting video_effects/video_chroma_shift
    video_effects/video_echo video_effects/video_interlace keying/luma_keyer
    keying/keying_chroma_keyer keying/keying_color_keyer keying/keying_despill
    keying/keying_garbage_matte keying/keying_difference_keyer
    keying/keying_matte_choker compositing/blend compositing/alpha_composite
    compositing/screen compositing/multiply compositing/overlay
    compositing/chroma_key compositing/luma_matte compositing/difference
    compositing/add color_calibration/calib_white_balance
    color_calibration/calib_hdr_tone_map color_calibration/calib_gamma_curve
    color_calibration/calib_black_level color_calibration/calib_white_point
    color_calibration/calib_color_space_transform
    color_calibration/calib_log_to_linear color_calibration/calib_legal_to_full
    color_calibration/calib_rec709_to_rec2020
    color_calibration/calib_srgb_to_display_p3
    color_calibration/calib_matrix_transform
    color_calibration/calib_full_to_legal color_grading/grade_teal_orange
    color_grading/grade_black_and_white color_grading/grade_split_toning
    color_grading/grade_duotone color_grading/grade_sepia
    color_grading/grade_shadows_highlights color_grading/grade_cross_process
    color_grading/grade_photo_filter color_grading/grade_color_wheels
    color_grading/grade_film_emulation color_grading/grade_kodachrome
    color_grading/grade_velvia color_grading/grade_portra
    color_grading/grade_bleach_bypass color_grading/grade_color_wash
    stylize/stylize_cartoon stylize/stylize_pixel_art stylize/stylize_neon
    stylize/stylize_thermal stylize/stylize_night_vision
    stylize/stylize_watercolor stylize/stylize_pencil_sketch
    stylize/stylize_oil_paint stylize/stylize_ink_outline
    stylize/stylize_line_art stylize/stylize_8bit stylize/stylize_comic_book
    stylize/stylize_duotone_art stylize/stylize_contour stylize/stylize_xray
    transition/cross_dissolve transition/wipe transition/zoom_transition
    transition/slide_transition transition/luma_wipe transition/cube_flip
    mask_matte/rect_mask mask_matte/ellipse_mask mask_matte/feather_mask
    mask_matte/track_matte mask_matte/bezier_mask time/frame_delay
    time/time_remap time/motion_trail time/frame_blend time/freeze_frame
    geometry/stroke_path geometry/boolean_op geometry/offset_path
    geometry/fill_shape geometry/shape_morph data_analysis/motion_detect
    data_analysis/histogram_compute data_analysis/edge_detect
    temporal/temporal_frame_average temporal/temporal_frame_median nle/nle_cut
    color_calibration/calib_hdr_maxcll color_calibration/calib_hdr_maxfall
    color_calibration/calib_luminance_match color_calibration/calib_chroma_match
    color_calibration/calib_monitor_profile
    color_calibration/calib_primaries_adjust color_calibration/calib_1d_lut_calib
    color_calibration/calib_3d_lut_calib color_grading/grade_tritone
    color_grading/grade_gradient_map color_grading/grade_color_lookup
    color_grading/grade_hue_vs_hue color_grading/grade_hue_vs_sat
    color_grading/grade_hue_vs_luma color_grading/grade_sat_vs_sat
    color_grading/grade_luma_vs_sat color_grading/grade_rgb_curves
    color_grading/grade_luma_curves keying/keying_luma_keyer
    keying/keying_inner_outer keying/keying_extract_keyer
    keying/keying_linear_color keying/keying_color_difference
    keying/keying_screen_matte keying/keying_refine_hard
    keying/keying_refine_soft keying/keying_edge_blend
    stylize/stylize_mosaic_stained stylize/stylize_pointillism
    stylize/stylize_low_poly stylize/stylize_relief stylize/stylize_metal
    3d/ambient_occlusion 3d/depth_map_shade 3d/normal_map_light 3d/obj_render
    audio_reactive/audio_spectrum_bars audio_reactive/audio_waveform_draw
    compositing/comp_layer_3d_transform compositing/comp_layer_adjustment
    compositing/comp_layer_blend compositing/comp_layer_camera
    compositing/comp_layer_card_wipe compositing/comp_layer_cc_cylinder
    compositing/comp_layer_cc_sphere compositing/comp_layer_fractal_noise
    compositing/comp_layer_frame_blend compositing/comp_layer_gradient_wipe
    compositing/comp_layer_light compositing/comp_layer_luma_key
    compositing/comp_layer_mask compositing/comp_layer_motion_blur
    compositing/comp_layer_null compositing/comp_layer_opacity
    compositing/comp_layer_parenting compositing/comp_layer_pixel_motion
    compositing/comp_layer_precompose compositing/comp_layer_rolling_shutter
    compositing/comp_layer_stabilize compositing/comp_layer_stack
    compositing/comp_layer_time_remap compositing/comp_layer_track_matte
    compositing/comp_layer_warp_stabilize data_analysis/optical_flow
    data_analysis/track_motion_tracker nle/nle_chapter_marker
    nle/nle_frame_interpolation nle/nle_j_cut nle/nle_l_cut nle/nle_lower_third
    nle/nle_multicam_sync nle/nle_rate_stretch nle/nle_reverse_playback
    nle/nle_ripple_delete nle/nle_ripple_insert nle/nle_roll_edit
    nle/nle_safe_title_overlay nle/nle_slide_edit nle/nle_slip_edit
    nle/nle_speed_ramp nle/nle_split_edit nle/nle_timecode_overlay
    particle/particle_trail temporal/temporal_dejudder
    temporal/temporal_exposure_blend temporal/temporal_field_blend
    temporal/temporal_field_weave temporal/temporal_flicker_reduction
    temporal/temporal_frame_rate_down temporal/temporal_frame_rate_up
    temporal/temporal_hdr_merge temporal/temporal_interlace_blend
    temporal/temporal_judder_reduction temporal/temporal_motion_adaptive
    temporal/temporal_motion_compensate temporal/temporal_optical_flow_blur
    temporal/temporal_rolling_shutter_fix temporal/temporal_super_resolution
    temporal/temporal_temporal_denoise temporal/temporal_temporal_smooth
    temporal/temporal_warp_stabilizer text/text_kinetic text/text_path
    text/text_render text/text_scramble text/text_typewriter
    text_overlay/text_bug text_overlay/text_caption text_overlay/text_caption_608
    text_overlay/text_caption_708 text_overlay/text_chyron
    text_overlay/text_crawl text_overlay/text_karaoke
    text_overlay/text_lower_third text_overlay/text_roll
    text_overlay/text_subtitle text_overlay/text_supers
    text_overlay/text_teletext text_overlay/text_timecode
    text_overlay/text_title_safe text_overlay/text_watermark
    video_effects/video_deinterlace video_effects/video_dust_scratches
    video_effects/video_frame_hold video_effects/video_glitch_analog_degradation
    video_effects/video_glitch_crt_curvature
    video_effects/video_glitch_crt_scanlines video_effects/video_glitch_datamosh
    video_effects/video_glitch_digital_noise
    video_effects/video_glitch_pixel_sort video_effects/video_glitch_rgb_split
    video_effects/video_glitch_scanline_shift
    video_effects/video_glitch_vhs_chroma video_effects/video_glitch_vhs_tracking
    video_effects/video_halftone video_effects/video_jitter
    video_effects/video_roll
)
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

jfx_test_target(test_animation_drawing SOURCES frontends/test_animation_drawing.cpp LIBRARIES tilly)
jfx_register_test(animation_drawing test_animation_drawing)

# LSan sees backing pages; explicitly check remaining process-heap objects too.
get_property(memory_tests DIRECTORY PROPERTY TESTS)
foreach(test IN LISTS memory_tests)
    set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "TILLY_CHECK_LEAKS=1")
endforeach()
