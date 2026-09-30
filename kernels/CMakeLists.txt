set(EFFECT_NAMES brightness contrast invert grayscale saturation sepia opacity threshold posterize exposure tint gamma)
set(EFFECT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/effect_sources.h")
file(WRITE "${EFFECT_HEADER}" "/* Generated from the bundled Joltscript sources. */\n")
foreach(name IN LISTS EFFECT_NAMES)
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/color/${name}.jolt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" text)
    string(REPLACE "\\" "\\\\" text "${text}")
    string(REPLACE "\"" "\\\"" text "${text}")
    string(REPLACE "\n" "\\n\"\n\"" text "${text}")
    file(APPEND "${EFFECT_HEADER}" "static const char source_${name}[] = \"${text}\";\n")
endforeach()
add_library(jolt_effects STATIC src/effects.c)
target_include_directories(jolt_effects PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include> PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(jolt_effects PUBLIC joltscript_glue)
set_project_warnings(jolt_effects)

# Frame kernels use the CPU image-source profile. Keep the old JBC1 catalog
# unchanged: its callers have neither image dimensions nor resource bindings.
set(IMAGE_KERNELS
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
    utility/channel_merge
    fractal/julia_set fractal/barnsley_fern fractal/ifs_fractal
    light/bloom light/glow light/point_light light/lens_flare
    light/shadow_cast light/directional_light
    video_effects/video_noise_grain video_effects/video_pixelate
    video_effects/video_strobe video_effects/video_flicker
    video_effects/video_mosaic video_effects/video_ghosting
    video_effects/video_chroma_shift video_effects/video_echo
    video_effects/video_interlace
    keying/luma_keyer keying/keying_chroma_keyer keying/keying_color_keyer
    keying/keying_despill keying/keying_garbage_matte
    keying/keying_difference_keyer keying/keying_matte_choker
    compositing/blend compositing/alpha_composite compositing/screen
    compositing/multiply compositing/overlay
    compositing/chroma_key compositing/luma_matte compositing/difference
    compositing/add
    color_calibration/calib_white_balance color_calibration/calib_hdr_tone_map
    color_calibration/calib_gamma_curve color_calibration/calib_black_level
    color_calibration/calib_white_point color_calibration/calib_color_space_transform
    color_calibration/calib_log_to_linear color_calibration/calib_legal_to_full
    color_calibration/calib_rec709_to_rec2020 color_calibration/calib_srgb_to_display_p3
    color_calibration/calib_matrix_transform color_calibration/calib_full_to_legal
    color_grading/grade_teal_orange color_grading/grade_black_and_white
    color_grading/grade_split_toning color_grading/grade_duotone
    color_grading/grade_sepia color_grading/grade_shadows_highlights
    color_grading/grade_cross_process color_grading/grade_photo_filter
    color_grading/grade_color_wheels color_grading/grade_film_emulation
    color_grading/grade_kodachrome color_grading/grade_velvia
    color_grading/grade_portra color_grading/grade_bleach_bypass
    color_grading/grade_color_wash
    stylize/stylize_cartoon stylize/stylize_pixel_art stylize/stylize_neon
    stylize/stylize_thermal stylize/stylize_night_vision
    stylize/stylize_watercolor stylize/stylize_pencil_sketch
    stylize/stylize_oil_paint stylize/stylize_ink_outline
    stylize/stylize_line_art stylize/stylize_8bit
    stylize/stylize_comic_book stylize/stylize_duotone_art
    stylize/stylize_contour stylize/stylize_xray
    transition/cross_dissolve transition/wipe transition/zoom_transition
    transition/slide_transition transition/luma_wipe transition/cube_flip
    mask_matte/rect_mask mask_matte/ellipse_mask mask_matte/feather_mask
    mask_matte/track_matte mask_matte/bezier_mask
    time/frame_delay time/time_remap time/motion_trail
    time/frame_blend time/freeze_frame
    light/bloom light/glow light/point_light light/lens_flare
    light/shadow_cast light/directional_light
    fractal/julia_set fractal/barnsley_fern fractal/ifs_fractal
    geometry/stroke_path geometry/boolean_op geometry/offset_path
    geometry/fill_shape geometry/shape_morph
    keying/luma_keyer keying/keying_chroma_keyer keying/keying_color_keyer
    keying/keying_despill keying/keying_garbage_matte
    keying/keying_difference_keyer keying/keying_matte_choker
    compositing/blend compositing/alpha_composite compositing/screen
    compositing/multiply compositing/overlay
    compositing/chroma_key compositing/luma_matte compositing/difference
    compositing/add
    data_analysis/motion_detect data_analysis/histogram_compute
    data_analysis/edge_detect
    video_effects/video_strobe video_effects/video_flicker
    video_effects/video_mosaic video_effects/video_ghosting
    video_effects/video_chroma_shift video_effects/video_echo
    video_effects/video_interlace
    temporal/temporal_frame_average temporal/temporal_frame_median
    nle/nle_cut
    utility/passthrough utility/premultiply_alpha utility/format_convert
    utility/channel_split utility/clamp_values utility/remap_range
    utility/channel_merge)
set(IMAGE_HEADER "${CMAKE_CURRENT_BINARY_DIR}/image_sources.h")
file(WRITE "${IMAGE_HEADER}" "/* Generated from Joltscript sources. */\n")
foreach(path IN ITEMS common/image ${IMAGE_KERNELS})
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${path}.jolt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    get_filename_component(name "${path}" NAME)
    # Byte arrays avoid ISO C's 4095-character string literal portability limit.
    file(READ "${source}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    file(APPEND "${IMAGE_HEADER}" "static const char image_source_${name}[] = {${bytes}0};\n")
endforeach()
file(APPEND "${IMAGE_HEADER}" "static const struct { const char *name; const char *source; } image_catalog[] = {\n")
foreach(path IN LISTS IMAGE_KERNELS)
    get_filename_component(name "${path}" NAME)
    file(APPEND "${IMAGE_HEADER}" "{\"${name}\", image_source_${name}},\n")
endforeach()
file(APPEND "${IMAGE_HEADER}" "};\n")
target_sources(jolt_effects PRIVATE src/image_kernels.c)