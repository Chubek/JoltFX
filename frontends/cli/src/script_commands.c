#include "commands.h"
#include "jfx/ffi_bridge.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SCRIPT_SOURCE_LIMIT (1024u * 1024u)
static char *read_document(const char *path, size_t limit, size_t *out_length) {
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return NULL; }
    tilly_allocator_t *allocator = (tilly_allocator_t *)tilly_default_allocator();
    char *text = tilly_alloc(allocator, limit + 1, _Alignof(max_align_t));
    if (!text) { fclose(file); return NULL; }
    size_t length = fread(text, 1, limit + 1, file); int failed = ferror(file); fclose(file);
    if (failed || length > limit) { tilly_free(allocator, text); return NULL; }
    text[length] = 0; *out_length = length; return text;
}
static void print_value(const jfx_value_t *value) {
    switch (value->type) {
        case JFX_TYPE_NIL: break;
        case JFX_TYPE_BOOL: puts(value->b ? "true" : "false"); break;
        case JFX_TYPE_INT: printf("%" PRId64 "\n", value->i); break;
        case JFX_TYPE_FLOAT: printf("%.17g\n", value->f); break;
        case JFX_TYPE_STRING: puts(value->str); break;
        case JFX_TYPE_COLOR: printf("0x%08" PRIx32 "\n", value->color); break;
        case JFX_TYPE_VEC2: case JFX_TYPE_VEC3: case JFX_TYPE_VEC4:
            for (size_t i = 0; i < (size_t)(value->type - JFX_TYPE_VEC2 + 2); ++i)
                printf("%s%.9g", i ? " " : "", (double)value->vec4[i]);
            putchar('\n'); break;
        default: puts("[jfx value]"); break;
    }
}
int cmd_scripts(int argc, char **argv) {
    if (argc == 1 && (!strcmp(argv[0], "--help") || !strcmp(argv[0], "-h"))) {
        print_command_help("scripts"); return 0;
    }
    if (argc == 1 && !strcmp(argv[0], "list")) {
        for (jfx_script_language_t language = JFX_SCRIPT_LUA; language < JFX_SCRIPT_LANGUAGE_COUNT; ++language)
            printf("%s %s\n", jfx_script_language_name(language), jfx_script_language_available(language) ? "enabled" : "disabled");
        return 0;
    }
    int edit = argc >= 1 && !strcmp(argv[0], "edit");
    if ((!edit && (argc < 3 || argc > 5 || strcmp(argv[0], "run"))) || (edit && (argc < 5 || argc > 6))) {
        print_command_help("scripts"); return 1;
    }
    jfx_script_language_t language = JFX_SCRIPT_LANGUAGE_COUNT;
    for (jfx_script_language_t candidate = JFX_SCRIPT_LUA; candidate < JFX_SCRIPT_LANGUAGE_COUNT; ++candidate)
        if (!strcmp(argv[1], jfx_script_language_name(candidate))) language = candidate;
    if (!jfx_script_language_available(language)) { fprintf(stderr, "Extension language '%s' is not enabled in this build.\n", argv[1]); return 1; }
    jfx_value_t argument = { .type = JFX_TYPE_FLOAT }, result = {0};
    if (!edit && argc == 5) {
        char *end = NULL; errno = 0; argument.f = strtod(argv[4], &end);
        if (errno || !*argv[4] || *end || !isfinite(argument.f)) {
            fprintf(stderr, "Script argument must be one finite number.\n"); return 1;
        }
    }
    jfx_engine_t *engine = NULL; jfx_engine_config_t config = {0};
    jfx_editor_t *editor = NULL; jfx_script_runtime_t *rt = NULL;
    char *source = NULL, *project = NULL; size_t length = 0;
    tilly_allocator_t *allocator = (tilly_allocator_t *)tilly_default_allocator(); int exit_code = 1;
    if (jfx_engine_init(&config, &engine) != JFX_SUCCESS || !(editor = jfx_editor_create(320, 180))) goto done;
    if (edit) {
        project = read_document(argv[3], JFX_PROJECT_MAX_BYTES, &length);
        char error[256] = {0};
        if (!project || jfx_editor_load(editor, project, length, error, sizeof(error)) != JFX_SUCCESS) {
            fprintf(stderr, "Project: %s\n", error); goto done;
        }
        tilly_free(allocator, project); project = NULL;
    }
    source = read_document(argv[2], SCRIPT_SOURCE_LIMIT, &length);
    if (!source) goto done;
    jfx_script_desc_t desc = { .size = sizeof(desc), .editor = editor,
        .capabilities = JFX_SCRIPT_CAP_EDITOR | JFX_SCRIPT_CAP_EVENTS };
    jfx_script_status_t status = jfx_script_runtime_create(language, &desc, &rt);
    if (!status) status = jfx_script_runtime_load(rt, source, length, argv[2]);
    if (!status && ((edit && argc == 6) || (!edit && argc >= 4))) {
        const char *function = argv[edit ? 5 : 3];
        status = jfx_script_runtime_call(rt, function, !edit && argc == 5 ? &argument : NULL,
            !edit && argc == 5 ? 1 : 0, &result);
    }
    if (status) { fprintf(stderr, "Extension: %s (%d)\n", jfx_script_runtime_last_error(rt), status); goto done; }
    if (!edit) { print_value(&result); exit_code = 0; goto done; }
    project = tilly_alloc(allocator, JFX_PROJECT_MAX_BYTES, _Alignof(max_align_t));
    if (!project || jfx_editor_save(editor, project, JFX_PROJECT_MAX_BYTES, &length) != JFX_SUCCESS) goto done;
    /* Script/load/call failures have already returned, before touching output. */
    FILE *file = fopen(argv[4], "wb");
    if (!file) { perror(argv[4]); goto done; }
    int failed = fwrite(project, 1, length, file) != length;
    if (fclose(file)) failed = 1;
    exit_code = failed ? 1 : 0;
done:
    tilly_free(allocator, project); tilly_free(allocator, source);
    jfx_script_runtime_destroy(rt); jfx_editor_destroy(editor); jfx_engine_shutdown(engine);
    return exit_code;
}
