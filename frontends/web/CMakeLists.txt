add_library(jfx_web_session STATIC src/jfx_wasm.c)
target_include_directories(jfx_web_session PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(jfx_web_session PUBLIC jfx_core jolt_effects)
set_project_warnings(jfx_web_session)

if(EMSCRIPTEN)
    # A runnable module for the TypeScript editor, including its filesystem and
    # shared color catalogue. Static libraries alone do not export callable JS.
    add_executable(joltfx_web src/wasm_entry.c)
    set_target_properties(joltfx_web PROPERTIES SUFFIX ".js")
    target_link_libraries(joltfx_web PRIVATE jfx_web_session)
    target_link_options(joltfx_web PRIVATE
        "--no-entry"
        "-sEXPORTED_FUNCTIONS=['_malloc','_free','_jfx_web_alloc','_jfx_web_free','_jfx_web_session_create','_jfx_web_session_destroy','_jfx_web_session_set_effect','_jfx_web_session_render_rgba','_jfx_web_session_render_frame','_jfx_web_session_render_graph','_jfx_web_session_load_document','_jfx_web_session_save_document','_jfx_web_session_save_sequence','_jfx_web_session_edit','_jfx_web_session_sequence_state','_jfx_web_session_graph_state','_jfx_web_session_render_audio','_jfx_web_session_audio_mixer','_jfx_web_audio_mixer_render','_jfx_audio_mixer_destroy','_jfx_web_session_export_begin','_jfx_web_export_completed','_jfx_export_available','_jfx_export_codec_available','_jfx_export_step','_jfx_export_state','_jfx_export_cancel','_jfx_export_destroy','_jfx_color_catalog','_jfx_node_catalog']"
        "-sEXPORTED_RUNTIME_METHODS=['cwrap','FS','HEAPU8']" "-sFORCE_FILESYSTEM=1"
        "-sMODULARIZE=1" "-sEXPORT_NAME=JoltFX" "-sALLOW_MEMORY_GROWTH=1"
        "-sINITIAL_MEMORY=33554432" "-sMAXIMUM_MEMORY=268435456" "-sSTACK_SIZE=1048576")
endif()
