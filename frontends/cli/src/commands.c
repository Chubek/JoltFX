#include "tilly/memory.h"
#include "commands.h"
#include "jfx/jfx_export.h"

#include "headless_frontend.h"
#include "jfx/jfx_engine.h"
#include "jfx/jfx_result.h"
#include "jfx_frontend.h"
#include "joltscript/compiler.h"
#include "joltscript/vm.h"
#include "joltscript/effects.h"
#include "tilly/logger.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOLTFX_CLI_VERSION "0.2.0"
/* Matches the compiler's 1 MiB source cap (see compiler.h). */
#define CLI_MAX_SOURCE ((size_t)1024 * 1024)
#define CLI_DEFAULT_MEMORY_LIMIT ((size_t)64 * 1024 * 1024)

void print_usage(void) {
    printf("joltfx %s - JoltFX effects engine CLI\n\n", JOLTFX_CLI_VERSION);
    printf("Usage: joltfx <command> [options]\n\n");
    printf("Commands:\n");
    printf("  compile FILE [-o OUTPUT]   Validate a .jolt kernel, optionally write bytecode\n");
    printf("  verify FILE                Validate a .jolt kernel without writing output\n");
    printf("  effects                    List bundled kernels\n");
    printf("  info EFFECT|FILE           Show effect or kernel details\n");
    printf("  render [options]           Render one frame to a binary PPM (P6)\n");
    printf("  export [options]           Render a frame range to a PPM sequence\n");
    printf("  export-video DOC.jfx -o OUT.mp4  Encode a sequence or composition with mixed audio\n");
    printf("  capabilities               Report this build's frontend capabilities\n");
    printf("  version                    Show version information\n");
    printf("  help [COMMAND]             Show help\n");
    printf("\nNon-Linear Editor (dedicated section):\n");
    printf("  nle new OUT.jfx [--size W H] [--fps NUM DEN]\n");
    printf("  nle edit IN.jfx OUT.jfx     Shared terminal editor, or pipe edit commands\n");
    printf("  nle info IN.jfx            NLE state as JSON\n");
    printf("  nle render IN.jfx -o PREFIX Render a PPM sequence\n");
    printf("  compose new/edit/info/render Node-based composition editor and PPM export\n");
    printf("\nColor Grading (dedicated section):\n");
    printf("  grade list                 List grading kernels and parameter ranges\n");
    printf("  grade apply KIND IN OUT    Grade an image; append name=value or --lut FILE\n");
    printf("\nColor Calibration (dedicated section):\n");
    printf("  calibration list           List calibration kernels and parameter ranges\n");
    printf("  calibration apply KIND IN OUT [name=value ...] [--lut FILE]\n");
    printf("  edit IN.jfx OUT.jfx         Terminal editor: grade.* and calibration.* commands\n");
    printf("  plugins inspect MODULE     Inspect SDK module and actions\n");
    printf("  plugins render MODULE IN OUT.ppm [FRAME]  Render a plugin project\n");
    printf("\nExtension scripts:\n");
    printf("  scripts list              List enabled extension languages\n");
    printf("  scripts run LANG FILE [FUNCTION [NUMBER]]  Load/call a sandboxed script\n");
    printf("  scripts edit LANG FILE IN.jfx OUT.jfx [FUNCTION]  Script the shared editor\n");
    printf("\nRun 'joltfx help COMMAND' for command-specific options.\n");
}

void print_command_help(const char *command) {
    if (command && !strcmp(command, "scripts")) {
        printf("Usage: joltfx scripts list\n"
            "       joltfx scripts run LANG FILE [FUNCTION [NUMBER]]\n"
            "       joltfx scripts edit LANG FILE INPUT.jfx OUTPUT.jfx [FUNCTION]\n"
            "LANG: lua, mruby, quickjs, python (MicroPython), wasm (WAMR).\n"
            "Only languages enabled at build time are available. WASM accepts .wasm or WAT.\n"
            "run prints the typed function result; NUMBER is one finite double argument.\n"
            "edit loads the input project, runs the script and optional zero-argument function,\n"
            "then saves on success. Errors leave existing project output untouched.\n"
            "Defaults: 4 MiB runtime memory, 100,000 instruction-budget units per invocation.\n"
            "Use jfx.command/state, JFX.command/state (mruby), or import jfx (MicroPython).\n"
            "See docs/extensions.md and extif/examples for bindings and embedding.\n");
        return;
    }
    if (command && !strcmp(command,"plugins")) {
        printf("Usage: joltfx plugins inspect MODULE\n"
            "       joltfx plugins render MODULE INPUT.jfx OUTPUT.ppm [FRAME]\n"
            "       joltfx edit INPUT.jfx OUTPUT.jfx [--plugin MODULE ...]\n"
            "       joltfx export-video INPUT.jfx -o OUTPUT.mkv [--plugin MODULE ...]\n"
            "Terminal: plugin.load PATH, plugin.unload ID, plugin.action NAME TRACK CLIP NODE, plugins.\n");
        return;
    }
    if (command && !strcmp(command,"export-video")) {
        printf("Usage: joltfx export-video DOC.jfx -o OUT.mp4|OUT.mov|OUT.mkv\n"
            "       [--plugin MODULE ...]\n"
            "       [--start FRAME --frames COUNT] [--width W --height H]\n"
            "       [--codec ENCODER --audio-codec ENCODER] [--sample-rate HZ] [--no-audio]\n"
            "Count 0 exports the remaining sequence; graph exports require a count (30/1 fps).\n"
            "Default codecs: MP4 MPEG-4/AAC, MOV ProRes/PCM, MKV FFV1/PCM.\n"
            "Also available as 'export DOC.jfx', 'nle export DOC.jfx', 'compose export DOC.jfx'.\n"
            "Encoder names (e.g. libx264) must exist in the linked FFmpeg build.\n"); return;
    }
    if (command && !strcmp(command,"compose")) {
        printf("Usage: joltfx compose new OUT.jfx [--size W H]\n"
            "       joltfx compose edit IN.jfx OUT.jfx\n"
            "       joltfx compose info IN.jfx\n"
            "       joltfx compose render IN.jfx -o OUT.ppm [--time S] [--node N] [--size W H]\n"
            "Terminal commands: node.add/connect/disconnect/param/path/label/position/duplicate/reset/remove/output,\n"
            "graph.size/new, undo, redo, composition (JSON state), nodes (library), show, save.\n"
            "Commands use OP A B C VALUE TEXT, with zero-based node/port indices.\n"
            "See docs/composition.md for port typing, layout, history and examples.\n"); return;
    }
    if (command && (!strcmp(command,"nle") || !strcmp(command,"edit"))) {
        printf("Usage: joltfx nle new OUTPUT.jfx [--size W H] [--fps NUM DEN]\n"
            "       joltfx nle edit INPUT.jfx OUTPUT.jfx\n"
            "       joltfx nle info INPUT.jfx\n"
            "       joltfx nle render INPUT.jfx -o PREFIX [--start N --end N]\n"
            "Editor syntax: OP TRACK CLIP TARGET VALUE TEXT (zero-based indices).\n"
            "NLE commands: clip.add/trim/move/split/duplicate/slip/remove/ripple_delete,\n"
            "track.add/remove/name/move/mute/solo/insert_gap. Also: undo, redo, timeline, show, save, quit.\n"
            "Audio: clip.audio.enabled/gain/pan/fade_in/fade_out; track.audio.gain.\n"
            "See docs/nle.md for timing and source in-point semantics.\n"); return;
    }
    if (command && (!strcmp(command,"grade") || !strcmp(command,"calibration"))) {
        printf("Usage: joltfx %s list\n       joltfx %s apply KIND INPUT OUTPUT.ppm [name=value ...] [--lut FILE]\n",command,command);
        printf("Kernels run through the shared graph and budgeted CPU executor.\n"
            "For ordered, animated grades use 'edit' and 'project render'.\n");
        return;
    }
    if (command == NULL || strcmp(command, "compile") == 0) {
        printf("Usage: joltfx compile FILE [-o OUTPUT]\n\n");
        printf("Validates a Joltscript kernel (MVP defkernel form) and, with -o,\n");
        printf("writes the compiled JBC1 bytecode to OUTPUT.\n");
    }
    if (command == NULL || strcmp(command, "verify") == 0) {
        printf("Usage: joltfx verify FILE\n\n");
        printf("Validates a Joltscript kernel. Exits 0 when valid, 1 otherwise.\n");
    }
    if (command == NULL || strcmp(command, "effects") == 0) {
        printf("Usage: joltfx effects\n\n");
        printf("Lists the bundled effect kernels (12 color effects).\n");
    }
    if (command == NULL || strcmp(command, "info") == 0) {
        printf("Usage: joltfx info EFFECT|FILE\n\n");
        printf("Shows details for a bundled effect name or a .jolt source file.\n");
    }
    if (command == NULL || strcmp(command, "render") == 0) {
        printf("Usage: joltfx render [--effect NAME] [--param VALUE]\n");
        printf("                     [--width W --height H] [-o OUTPUT.ppm]\n");
        printf("                     [--backend NAME]\n\n");
        printf("Renders a bundled effect over an animated gradient to a binary PPM (P6)\n");
        printf("through the Core engine, so --backend selects the real execution path.\n");
        printf("Defaults: --effect brightness --width 64 --height 64 -o render.ppm\n");
        printf("Backend NAME is one the engine was built with: vulkan, metal, d3d12,\n");
        printf("webgpu or auto.\n");
    }
    if (command == NULL || strcmp(command, "export") == 0) {
        printf("Usage: joltfx export [--effect NAME] [--param VALUE]\n");
        printf("                    [--width W --height H] [--start N --end N]\n");
        printf("                    [-o DIRECTORY] [--backend NAME]\n\n");
        printf("Renders a frame range to one binary PPM (P6) per frame, named\n");
        printf("frame_%%06d.ppm, at 24 fps. Frames are inclusive; --start defaults to 0\n");
        printf("and --end to 47. Defaults: --effect brightness --width 64 --height 64\n");
        printf("-o frames.\n");
    }
    if (command != NULL && strcmp(command, "capabilities") == 0) {
        printf("Usage: joltfx capabilities\n\n");
        printf("Reports which shared frontend-contract operations this build provides.\n");
    }
}

/* Reads a whole file into a NUL-terminated buffer. Returns NULL on error or
 * when the file exceeds CLI_MAX_SOURCE, which bounds both source text and
 * compiled bytecode (the compiler caps sources at 1 MiB and a JBC1 program can
 * only be a little larger than its source). */
static char *read_source(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long tell = ftell(fp);
    if (tell < 0 || (size_t)tell > CLI_MAX_SOURCE) {
        fclose(fp);
        return NULL;
    }
    size_t len = (size_t)tell;
    rewind(fp);
    char *buf = (char *)tilly_mem_alloc(len + 1);
    if (buf == NULL) {
        fclose(fp);
        return NULL;
    }
    size_t got = fread(buf, 1, len, fp);
    fclose(fp);
    if (got != len) {
        tilly_mem_free(buf);
        return NULL;
    }
    buf[len] = '\0';
    if (out_len != NULL) {
        *out_len = len;
    }
    return buf;
}

/* Same, but reports whether the file begins with a JBC1 magic so callers can
 * tell a compiled artifact from source without a second read. Returns NULL for
 * anything that is not JBC1. */
static uint8_t *read_bytecode(const char *path, size_t *out_len) {
    size_t len = 0;
    char *bytes = read_source(path, &len);
    if (bytes == NULL) {
        return NULL;
    }
    if (len < JOLT_BYTECODE_HEADER_SIZE) {
        tilly_mem_free(bytes);
        return NULL;
    }
    uint32_t magic = 0;
    for (unsigned i = 0; i < 4; ++i) {
        magic |= (uint32_t)(unsigned char)bytes[i] << (8u * i);
    }
    if (magic != JOLT_BYTECODE_MAGIC) {
        tilly_mem_free(bytes);
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = len;
    }
    return (uint8_t *)bytes;
}

/* Best-effort kernel name extraction for info output. */
static void extract_kernel_name(const char *source, char *out, size_t out_size) {
    const char *at = strstr(source, "defkernel");
    if (at == NULL || out_size == 0) {
        snprintf(out, out_size, "(unknown)");
        return;
    }
    at += strlen("defkernel");
    while (*at != '\0' && isspace((unsigned char)*at)) {
        at++;
    }
    size_t i = 0;
    while (*at != '\0' && !isspace((unsigned char)*at) && *at != '[' && *at != '(' &&
           *at != '\n' && i + 1 < out_size) {
        out[i++] = *at++;
    }
    out[i] = '\0';
    if (i == 0) {
        snprintf(out, out_size, "(unknown)");
    }
}

static int compile_source(const char *path, const char *source, jolt_program_t **out) {
    jolt_diagnostic_t diag;
    memset(&diag, 0, sizeof(diag));
    diag.size = sizeof(diag);
    jolt_program_t *program = NULL;
    jolt_status_t status = jolt_compile(source, &program, &diag);
    if (status != JOLT_OK || program == NULL) {
        if (diag.message[0] != '\0') {
            fprintf(stderr, "error: %s:%zu:%zu: %s\n", path, diag.line, diag.column,
                    diag.message);
        } else {
            fprintf(stderr, "error: %s: compilation failed (status %d)\n", path,
                    (int)status);
        }
        if (program != NULL) {
            jolt_program_destroy(program);
        }
        return 1;
    }
    *out = program;
    return 0;
}

int cmd_compile(int argc, char **argv) {
    const char *input = NULL;
    const char *output = NULL;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires a value\n\n", argv[i]);
                print_command_help("compile");
                return 1;
            }
            output = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("compile");
            return 0;
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
            print_command_help("compile");
            return 1;
        } else if (input == NULL) {
            input = argv[i];
        } else {
            fprintf(stderr, "error: expected one FILE, got several\n\n");
            print_command_help("compile");
            return 1;
        }
    }
    if (input == NULL) {
        fprintf(stderr, "error: missing FILE\n\n");
        print_command_help("compile");
        return 1;
    }
    char *source = read_source(input, NULL);
    if (source == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", input);
        return 1;
    }
    jolt_program_t *program = NULL;
    int rc = compile_source(input, source, &program);
    tilly_mem_free(source);
    if (rc != 0) {
        return 1;
    }
    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    if (data == NULL) {
        fprintf(stderr, "error: %s: compiled program has no bytecode\n", input);
        jolt_program_destroy(program);
        return 1;
    }
    if (output != NULL) {
        FILE *fp = fopen(output, "wb");
        if (fp == NULL) {
            fprintf(stderr, "error: cannot write '%s'\n", output);
            jolt_program_destroy(program);
            return 1;
        }
        size_t wrote = fwrite(data, 1, size, fp);
        fclose(fp);
        if (wrote != size) {
            fprintf(stderr, "error: short write to '%s'\n", output);
            jolt_program_destroy(program);
            return 1;
        }
        printf("ok: %s: %zu bytes -> %s\n", input, size, output);
    } else {
        printf("ok: %s: %zu bytes\n", input, size);
    }
    jolt_program_destroy(program);
    return 0;
}

int cmd_verify(int argc, char **argv) {
    if (argc == 1 && (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0)) {
        print_command_help("verify");
        return 0;
    }
    if (argc != 1) {
        fprintf(stderr, "error: verify takes exactly one FILE\n\n");
        print_command_help("verify");
        return 1;
    }
    const char *input = argv[0];
    char *source = read_source(input, NULL);
    if (source == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", input);
        return 1;
    }
    jolt_program_t *program = NULL;
    int rc = compile_source(input, source, &program);
    tilly_mem_free(source);
    if (rc != 0) {
        return 1;
    }
    size_t size = 0;
    (void)jolt_program_data(program, &size);
    printf("ok: %s: %zu bytes\n", input, size);
    jolt_program_destroy(program);
    return 0;
}

int cmd_effects(int argc, char **argv) {
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("effects");
            return 0;
        }
        fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
        print_command_help("effects");
        return 1;
    }
    jolt_effects_t *effects = jolt_effects_create();
    if (effects == NULL) {
        fprintf(stderr, "error: failed to load bundled effects\n");
        return 1;
    }
    size_t count = jolt_effects_count(effects);
    for (size_t i = 0; i < count; i++) {
        printf("%s\n", jolt_effects_name(i));
    }
    jolt_effects_destroy(effects);
    return 0;
}

/* Reports a JBC1 program: the header the VM validates, and the instruction
 * mix. Accepts anything `compile` produced, including bytecode from the Zoltan
 * compiler, so a compiled artifact can be inspected without its source. */
static int info_for_bytecode(const char *path, const uint8_t *bytes, size_t size) {
    if (size < JOLT_BYTECODE_HEADER_SIZE) {
        fprintf(stderr, "error: %s: too short to be a JBC1 program\n", path);
        return 1;
    }
    uint32_t read = 0;
    for (unsigned i = 0; i < 4; ++i) {
        read |= (uint32_t)bytes[i] << (8u * i);
    }
    if (read != JOLT_BYTECODE_MAGIC) {
        fprintf(stderr, "error: %s: not a JBC1 program\n", path);
        return 1;
    }
    uint32_t version = 0, inputs = 0, outputs = 0;
    for (unsigned i = 0; i < 4; ++i) {
        version |= (uint32_t)bytes[4 + i] << (8u * i);
        inputs |= (uint32_t)bytes[8 + i] << (8u * i);
        outputs |= (uint32_t)bytes[12 + i] << (8u * i);
    }
    if (version != JOLT_BYTECODE_VERSION) {
        fprintf(stderr, "error: %s: unsupported JBC1 version %u\n", path, version);
        return 1;
    }
    if (jolt_bytecode_validate(bytes, size) != JOLT_OK) {
        fprintf(stderr, "error: %s: invalid JBC1 program\n", path);
        return 1;
    }
    const size_t instructions = (size - JOLT_BYTECODE_HEADER_SIZE) / 8u;
    printf("file: %s\nformat: JBC1 v%u\ninputs: %u\noutputs: %u\ninstructions: %zu\n"
           "bytecode: %zu bytes\n",
        path, version, inputs, outputs, instructions, size);
    return 0;
}

static int info_for_file(const char *path) {
    /* A .jbc artifact has no source to compile; inspect the container. */
    size_t raw_size = 0;
    uint8_t *raw = read_bytecode(path, &raw_size);
    if (raw != NULL) {
        int rc = info_for_bytecode(path, raw, raw_size);
        tilly_mem_free(raw);
        return rc;
    }
    char *source = read_source(path, NULL);
    if (source == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        return 1;
    }
    jolt_program_t *program = NULL;
    int rc = compile_source(path, source, &program);
    if (rc != 0) {
        tilly_mem_free(source);
        return 1;
    }
    size_t size = 0;
    (void)jolt_program_data(program, &size);
    char name[64];
    extract_kernel_name(source, name, sizeof(name));
    printf("file: %s\nkernel: %s\nbytecode: %zu bytes\n", path, name, size);
    tilly_mem_free(source);
    jolt_program_destroy(program);
    return 0;
}

static int info_for_effect(const char *name) {
    jolt_effects_t *effects = jolt_effects_create();
    if (effects == NULL) {
        fprintf(stderr, "error: failed to load bundled effects\n");
        return 1;
    }
    float input[4] = {0.25f, 0.5f, 0.75f, 1.0f};
    float output[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    jolt_status_t status = jolt_effects_apply(effects, name, input, 1, NULL, 0,
                                              CLI_DEFAULT_MEMORY_LIMIT, output);
    if (status != JOLT_OK) {
        fprintf(stderr, "error: unknown effect '%s'\n", name);
        jolt_effects_destroy(effects);
        return 1;
    }
    printf("effect: %s\n", name);
    printf("sample: in=(%.3f %.3f %.3f %.3f) out=(%.3f %.3f %.3f %.3f)\n",
           (double)input[0], (double)input[1], (double)input[2], (double)input[3],
           (double)output[0], (double)output[1], (double)output[2],
           (double)output[3]);
    jolt_effects_destroy(effects);
    return 0;
}

int cmd_info(int argc, char **argv) {
    if (argc != 1) {
        fprintf(stderr, "error: info takes exactly one EFFECT or FILE\n\n");
        print_command_help("info");
        return 1;
    }
    if (strcmp(argv[0], "-h") == 0 || strcmp(argv[0], "--help") == 0) {
        print_command_help("info");
        return 0;
    }
    FILE *probe = fopen(argv[0], "rb");
    if (probe != NULL) {
        fclose(probe);
        return info_for_file(argv[0]);
    }
    return info_for_effect(argv[0]);
}

/* ---- render and export, both driven through the shared frontend contract --
 *
 * These commands do not have a private render path: they create the headless
 * frontend and go through jfx_frontend_render_frame /
 * jfx_frontend_export_frames, so the CLI and the GUI frontends execute the
 * same code. The frame still runs on whichever backend the engine resolved. */

typedef struct {
    jfx_frontend_t *frontend;
    char backend_name[64];
} cli_session_t;

static void cli_session_release(cli_session_t *session) {
    if (!session) {
        return;
    }
    jfx_frontend_shutdown(session->frontend);
    memset(session, 0, sizeof(*session));
}

/* Creates the headless frontend and selects the effect. */
static int cli_session_open(cli_session_t *session, const char *effect, float parameter,
    int have_parameter, const char *backend, uint32_t width, uint32_t height) {
    memset(session, 0, sizeof(*session));
    jfx_frontend_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.name = "cli";
    desc.width = width;
    desc.height = height;
    desc.backend_name = backend;
    jfx_result_t status = jfx_frontend_init(&jfx_headless_frontend_ops, "cli", &desc,
        &session->frontend);
    if (status != JFX_SUCCESS || !session->frontend) {
        fprintf(stderr, "error: could not start the engine (%s)\n",
            jfx_result_to_string(status));
        return 1;
    }
    if (jfx_headless_set_effect(session->frontend->state, effect,
            have_parameter ? parameter : 0.0f) != JFX_SUCCESS) {
        fprintf(stderr, "error: unknown effect '%s' (try 'joltfx effects')\n", effect);
        cli_session_release(session);
        return 1;
    }
    const char *resolved = jfx_headless_backend_name(session->frontend);
    snprintf(session->backend_name, sizeof(session->backend_name), "%s",
        resolved ? resolved : "none");
    return 0;
}

/* Writes a tightly packed RGBA8 buffer as a binary PPM (P6), dropping alpha. */
static int write_ppm(const char *path, const uint8_t *rgba, uint32_t width, uint32_t height) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        fprintf(stderr, "error: cannot write '%s'\n", path);
        return 1;
    }
    if (fprintf(file, "P6\n%u %u\n255\n", width, height) < 0) {
        fprintf(stderr, "error: cannot write the header of '%s'\n", path);
        fclose(file);
        return 1;
    }
    for (uint32_t y = 0; y < height; ++y) {
        const uint8_t *row = rgba + (size_t)y * width * 4u;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t rgb[3] = { row[x * 4u], row[x * 4u + 1u], row[x * 4u + 2u] };
            if (fwrite(rgb, 1, 3, file) != 3) {
                fprintf(stderr, "error: short write to '%s'\n", path);
                fclose(file);
                return 1;
            }
        }
    }
    if (fclose(file) != 0) {
        fprintf(stderr, "error: cannot flush '%s'\n", path);
        return 1;
    }
    return 0;
}

int cmd_render(int argc, char **argv) {
    const char *effect = "brightness";
    const char *output = "render.ppm";
    const char *backend = NULL;
    float param_value = 0.0f;
    int have_param = 0;
    long width = 64;
    long height = 64;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--effect") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --effect requires a value\n");
                return 1;
            }
            effect = argv[i];
        } else if (strcmp(argv[i], "--param") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --param requires a value\n");
                return 1;
            }
            char *end = NULL;
            double v = strtod(argv[i], &end);
            if (end == argv[i] || *end != '\0' || !isfinite(v)) {
                fprintf(stderr, "error: invalid --param '%s'\n", argv[i]);
                return 1;
            }
            param_value = (float)v;
            have_param = 1;
        } else if (strcmp(argv[i], "--width") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --width requires a value\n");
                return 1;
            }
            width = strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "--height") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --height requires a value\n");
                return 1;
            }
            height = strtol(argv[i], NULL, 10);
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: %s requires a value\n", argv[i - 1]);
                return 1;
            }
            output = argv[i];
        } else if (strcmp(argv[i], "--backend") == 0) {
            if (++i >= argc) {
                fprintf(stderr, "error: --backend requires a value\n");
                return 1;
            }
            backend = argv[i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("render");
            return 0;
        } else {
            fprintf(stderr, "error: unknown option '%s'\n\n", argv[i]);
            print_command_help("render");
            return 1;
        }
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        fprintf(stderr, "error: width/height must be in [1, 4096]\n");
        return 1;
    }

    cli_session_t session;
    if (cli_session_open(&session, effect, param_value, have_param, backend, (uint32_t)width,
            (uint32_t)height) != 0) {
        return 1;
    }

    const size_t frame_bytes = (size_t)width * (size_t)height * 4u;
    uint8_t *frame = tilly_mem_alloc(frame_bytes);
    if (frame == NULL) {
        fprintf(stderr, "error: out of memory\n");
        cli_session_release(&session);
        return 1;
    }
    jfx_result_t status = jfx_frontend_render_frame(session.frontend, (uint32_t)width,
        (uint32_t)height, frame, frame_bytes);
    if (status != JFX_SUCCESS) {
        fprintf(stderr, "error: render failed for effect '%s' (%s)\n", effect,
            jfx_result_to_string(status));
        tilly_mem_free(frame);
        cli_session_release(&session);
        return 1;
    }
    const int rc = write_ppm(output, frame, (uint32_t)width, (uint32_t)height);
    tilly_mem_free(frame);
    if (rc != 0) {
        cli_session_release(&session);
        return 1;
    }
    printf("rendered %ldx%ld %s -> %s (backend %s)\n", width, height, effect, output,
        session.backend_name);
    tilly_log_simple(TILLY_LOG_INFO, "CLI render complete");
    cli_session_release(&session);
    return 0;
}

static int export_progress(size_t current, size_t total, double elapsed, void *user_data) {
    (void)user_data;
    /* A single rewriting line, so a long export does not scroll the terminal. */
    fprintf(stderr, "\rExporting frame %zu/%zu (%.1fs)", current, total, elapsed);
    if (current == total) {
        fputc('\n', stderr);
    }
    return 0;
}

int cmd_export(int argc, char **argv) {
    if (argc>0 && argv[0][0]!='-') return cmd_media_export(argc,argv);
    const char *effect = "brightness";
    const char *output = "frames";
    const char *backend = NULL;
    float param_value = 0.0f;
    int have_param = 0;
    long width = 64;
    long height = 64;
    long start_frame = 0;
    long end_frame = 47;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--effect") == 0 && i + 1 < argc) {
            effect = argv[++i];
        } else if (strcmp(argv[i], "--param") == 0 && i + 1 < argc) {
            char *end = NULL;
            double v = strtod(argv[++i], &end);
            if (*end != '\0' || !isfinite(v)) {
                fprintf(stderr, "error: invalid --param\n");
                return 1;
            }
            param_value = (float)v;
            have_param = 1;
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            start_frame = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--end") == 0 && i + 1 < argc) {
            end_frame = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            backend = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_command_help("export");
            return 0;
        } else {
            fprintf(stderr, "error: %s expects a value or is unknown\n\n", argv[i]);
            print_command_help("export");
            return 1;
        }
    }
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) {
        fprintf(stderr, "error: width/height must be in [1, 4096]\n");
        return 1;
    }
    if (start_frame < 0 || end_frame < start_frame || end_frame - start_frame + 1 > 100000) {
        fprintf(stderr, "error: --start/--end must satisfy 0 <= start <= end <= start+100000\n");
        return 1;
    }

    cli_session_t session;
    if (cli_session_open(&session, effect, param_value, have_param, backend, (uint32_t)width,
            (uint32_t)height) != 0) {
        return 1;
    }
    jfx_result_t status = jfx_frontend_export_frames(session.frontend, output,
        (uint32_t)width, (uint32_t)height, (uint32_t)start_frame, (uint32_t)end_frame,
        export_progress, NULL);
    if (status != JFX_SUCCESS) {
        fprintf(stderr, "\nerror: export failed (%s)\n", jfx_result_to_string(status));
        cli_session_release(&session);
        return 1;
    }
    printf("exported frames %ld-%ld %s %ldx%ld to %s/ (backend %s)\n", start_frame, end_frame,
        effect, width, height, output, session.backend_name);
    cli_session_release(&session);
    return 0;
}

int cmd_capabilities(void) {
    printf("joltfx %s frontend capabilities\n\n", JOLTFX_CLI_VERSION);
    static const char *const kNames[] = { "run", "open_project", "save_project",
        "close_project", "playback", "loop", "render", "export", "selection",
        "viewport_state", "zoom" };
    jfx_frontend_t *probe = NULL;
    jfx_frontend_desc_t desc;
    memset(&desc, 0, sizeof(desc));
    desc.size = sizeof(desc);
    desc.name = "cli";
    /* A real engine is needed to report honestly, so create one. */
    if (jfx_frontend_init(&jfx_headless_frontend_ops, "cli", &desc, &probe) != JFX_SUCCESS) {
        fprintf(stderr, "error: could not start the engine to query capabilities\n");
        return 1;
    }
    const uint32_t capabilities = jfx_frontend_capabilities(probe);
    for (size_t i = 0; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
        const uint32_t bit = 1u << i;
        printf("  %-16s %s\n", kNames[i],
            (capabilities & bit) ? "yes" : "no (not implemented)");
    }
    printf("\nbackend: %s\n", jfx_headless_backend_name(probe));
    printf("audio_mixing: yes (WAV/FLAC/MP3; optional FFmpeg containers)\nencoded_video: %s\n",jfx_export_available()?"yes":"no (FFmpeg unavailable)");
    jfx_frontend_shutdown(probe);
    return 0;
}
