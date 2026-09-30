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
    utility/passthrough)
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
