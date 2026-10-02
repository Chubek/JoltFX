# The editor descriptor table is generated from executable source declarations,
# so UI ranges/defaults cannot drift from the kernel compiler's reflection.
set(JFX_COLOR_KERNELS
    color_grading/grade_primary color_grading/grade_lut
    color_calibration/calib_lut
    color_grading/grade_color_wheels color_grading/grade_shadows_highlights
    color_grading/grade_split_toning color_grading/grade_teal_orange
    color_grading/grade_black_and_white color_grading/grade_duotone
    color_grading/grade_sepia color_grading/grade_cross_process
    color_grading/grade_photo_filter color_grading/grade_film_emulation
    color_grading/grade_kodachrome color_grading/grade_velvia
    color_grading/grade_portra color_grading/grade_bleach_bypass
    color_grading/grade_color_wash
    color_calibration/calib_white_balance color_calibration/calib_hdr_tone_map
    color_calibration/calib_gamma_curve color_calibration/calib_black_level
    color_calibration/calib_white_point color_calibration/calib_color_space_transform
    color_calibration/calib_log_to_linear color_calibration/calib_legal_to_full
    color_calibration/calib_full_to_legal color_calibration/calib_rec709_to_rec2020
    color_calibration/calib_srgb_to_display_p3)
set(header "${CMAKE_CURRENT_BINARY_DIR}/color_nodes.inc")
file(WRITE "${header}" "/* Generated from kernels; do not edit. */\n")
set(kinds "")
foreach(path IN LISTS JFX_COLOR_KERNELS)
    set(source "${CMAKE_SOURCE_DIR}/kernels/${path}.jolt")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
    file(READ "${source}" text)
    get_filename_component(name "${path}" NAME)
    string(REGEX MATCHALL "\\(param [^\r\n]+\\)" params "${text}")
    list(LENGTH params count)
    if(count GREATER 12)
        message(FATAL_ERROR "${name} exceeds the node parameter budget")
    endif()
    file(APPEND "${header}" "static const jfx_param_desc_t params_${name}[] = {\n")
    foreach(param IN LISTS params)
        string(REGEX REPLACE "\\(param ([^ ]+) ([^ ]+) ([^ ]+) ([^ ]+) ([01])\\)" "\\1;\\2;\\3;\\4;\\5" fields "${param}")
        list(GET fields 0 id)
        list(GET fields 1 default)
        list(GET fields 2 min)
        list(GET fields 3 max)
        list(GET fields 4 integral)
        string(REPLACE "_" " " label "${id}")
        file(APPEND "${header}" "{\"${id}\",\"${label}\",(float)(${min}),(float)(${max}),(float)(${default}),${integral},${integral}},\n")
    endforeach()
    file(APPEND "${header}" "};\n")
    string(REGEX REPLACE "^(grade_|calib_)" "" label "${name}")
    string(REPLACE "_" " " label "${label}")
    if(path MATCHES "^color_calibration/")
        set(category "Color Calibration")
    else()
        set(category "Color Grading")
    endif()
    set(strings "0,NULL")
    if(name STREQUAL "grade_lut" OR name STREQUAL "calib_lut")
        set(strings "1,color_path")
    endif()
    string(APPEND kinds "{\"${name}\",\"${label}\",\"${category}\",1,color_in,1,color_out,${count},params_${name},${strings}},\n")
endforeach()
file(APPEND "${header}" "static const jfx_node_kind_t color_kinds[] = {\n${kinds}};\n")
