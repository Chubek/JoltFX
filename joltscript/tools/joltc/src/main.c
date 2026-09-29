/* joltc — Joltscript compiler driver
 *
 * Compiles .jolt source files to JBC1 bytecode.
 *
 * Usage: joltc [options] <input.jolt> [-o <output.jbc>]
 *
 * Options:
 *   -o, --output <path>  Write bytecode to <path>
 *   -h, --help           Show this help message
 *   -v, --version        Show version information
 *       --check          Validate only, don't emit bytecode
 *       --dump           Dump disassembly to stdout
 */

#include "joltscript/compiler.h"
#include "joltscript/image_program.h"
#include "tilly/containers.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define JOLTC_VERSION "0.2.0"

typedef struct {
    const char *input_path;
    const char *output_path;
    const char *image_library;
    bool check_only;
    bool dump_disasm;
} joltc_options_t;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input.jolt> [-o <output.jbc>]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o, --output <path>  Write bytecode to <path>\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --version        Show version information\n");
    fprintf(stderr, "      --check          Validate only, don't emit bytecode\n");
    fprintf(stderr, "      --image-library <path>  Check CPU image source with this Joltscript library\n");
    fprintf(stderr, "      --dump           Dump disassembly to stdout\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s kernel.jolt -o kernel.jbc\n", prog);
    fprintf(stderr, "  %s --check kernel.jolt\n", prog);
}

static void print_version(void) {
    printf("joltc %s — Joltscript compiler\n", JOLTC_VERSION);
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    if(fseek(f, 0, SEEK_END)!=0) { fclose(f); return NULL; }
    long size = ftell(f);
    if(fseek(f, 0, SEEK_SET)!=0) { fclose(f); return NULL; }
    if (size < 0 || size > 1024*1024) { fclose(f); return NULL; }
    char *buf = tilly_container_alloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)size, f);
    if(read!=(size_t)size || ferror(f)) { tilly_container_free(buf); fclose(f); return NULL; }
    buf[read] = 0;
    fclose(f);
    if (out_size) *out_size = read;
    return buf;
}

static int write_file(const char *path, const uint8_t *data, size_t size) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t written = fwrite(data, 1, size, f);
    fclose(f);
    return written == size ? 0 : -1;
}

static const char *opcode_name(uint32_t op) {
    switch (op) {
    case JOLT_OP_CONST: return "CONST";
    case JOLT_OP_INPUT: return "INPUT";
    case JOLT_OP_ADD: return "ADD";
    case JOLT_OP_SUB: return "SUB";
    case JOLT_OP_MUL: return "MUL";
    case JOLT_OP_DIV: return "DIV";
    case JOLT_OP_MIN: return "MIN";
    case JOLT_OP_MAX: return "MAX";
    case JOLT_OP_ABS: return "ABS";
    case JOLT_OP_FLOOR: return "FLOOR";
    case JOLT_OP_POW: return "POW";
    case JOLT_OP_SQRT: return "SQRT";
    case JOLT_OP_LT: return "LT";
    case JOLT_OP_SELECT: return "SELECT";
    case JOLT_OP_OUTPUT: return "OUTPUT";
    case JOLT_OP_GT: return "GT";
    case JOLT_OP_LE: return "LE";
    case JOLT_OP_GE: return "GE";
    case JOLT_OP_EQ: return "EQ";
    case JOLT_OP_NE: return "NE";
    case JOLT_OP_AND: return "AND";
    case JOLT_OP_OR: return "OR";
    case JOLT_OP_NOT: return "NOT";
    case JOLT_OP_BITWISE_AND: return "BITWISE_AND";
    case JOLT_OP_BITWISE_OR: return "BITWISE_OR";
    case JOLT_OP_BITWISE_XOR: return "BITWISE_XOR";
    case JOLT_OP_SHL: return "SHL";
    case JOLT_OP_SHR: return "SHR";
    default: return "UNKNOWN";
    }
}

static void dump_disasm(const uint8_t *code, size_t size) {
    if (size < JOLT_BYTECODE_HEADER_SIZE) {
        fprintf(stderr, "error: program too short for disassembly\n");
        return;
    }
    uint32_t magic = (uint32_t)code[0] | ((uint32_t)code[1] << 8) |
                     ((uint32_t)code[2] << 16) | ((uint32_t)code[3] << 24);
    uint32_t version = (uint32_t)code[4] | ((uint32_t)code[5] << 8) |
                       ((uint32_t)code[6] << 16) | ((uint32_t)code[7] << 24);
    uint32_t inputs = (uint32_t)code[8] | ((uint32_t)code[9] << 8) |
                      ((uint32_t)code[10] << 16) | ((uint32_t)code[11] << 24);
    uint32_t outputs = (uint32_t)code[12] | ((uint32_t)code[13] << 8) |
                       ((uint32_t)code[14] << 16) | ((uint32_t)code[15] << 24);
    printf("JBC1 disassembly (%zu bytes)\n", size);
    printf("  magic:    0x%08x\n", magic);
    printf("  version:  %u\n", version);
    printf("  inputs:   %u\n", inputs);
    printf("  outputs:  %u\n", outputs);
    printf("  instructions:\n");
    for (size_t pos = JOLT_BYTECODE_HEADER_SIZE; pos + 8 <= size; pos += 8) {
        uint32_t op = (uint32_t)code[pos] | ((uint32_t)code[pos+1] << 8) |
                      ((uint32_t)code[pos+2] << 16) | ((uint32_t)code[pos+3] << 24);
        uint32_t arg = (uint32_t)code[pos+4] | ((uint32_t)code[pos+5] << 8) |
                       ((uint32_t)code[pos+6] << 16) | ((uint32_t)code[pos+7] << 24);
        printf("    %4zu: %-12s 0x%08x", (pos - JOLT_BYTECODE_HEADER_SIZE) / 8, opcode_name(op), arg);
        if (op == JOLT_OP_CONST) {
            float f; memcpy(&f, &arg, 4);
            printf("  ; %g", f);
        } else if (op == JOLT_OP_INPUT) {
            printf("  ; input[%u]", arg);
        } else if (op == JOLT_OP_OUTPUT) {
            printf("  ; output[%u]", arg);
        }
        printf("\n");
    }
}

static int parse_args(int argc, char **argv, joltc_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(argv[i], "--check") == 0) {
            opts->check_only = true;
        } else if (strcmp(argv[i], "--image-library") == 0) {
            if(i+1>=argc) { fprintf(stderr,"error: --image-library requires a path\n"); return -1; }
            opts->image_library=argv[++i];
        } else if (strcmp(argv[i], "--dump") == 0) {
            opts->dump_disasm = true;
        } else if (strcmp(argv[i], "-o") == 0 || strcmp(argv[i], "--output") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires an argument\n", argv[i]);
                return -1;
            }
            opts->output_path = argv[++i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            return -1;
        } else {
            if (opts->input_path) {
                fprintf(stderr, "error: multiple input files specified\n");
                return -1;
            }
            opts->input_path = argv[i];
        }
        ++i;
    }
    if (!opts->input_path) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return -1;
    }
    if(opts->image_library && (!opts->check_only || opts->dump_disasm || opts->output_path)) {
        fprintf(stderr,"error: --image-library requires --check and cannot emit JBC1 or disassembly\n");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    joltc_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) return 1;

    size_t src_size = 0;
    char *source = read_file(opts.input_path, &src_size);
    if (!source) {
        fprintf(stderr, "error: cannot read '%s'\n", opts.input_path);
        return 1;
    }

    if(opts.image_library) {
        char *library=read_file(opts.image_library,NULL);
        if(!library) { fprintf(stderr,"error: cannot read '%s'\n",opts.image_library); tilly_container_free(source); return 1; }
        jolt_image_program_t *image=NULL;
        jolt_diagnostic_t diagnostic={.size=sizeof(diagnostic)};
        jolt_status_t result=jolt_image_compile(library,source,&image,&diagnostic);
        if(result==JOLT_OK) printf("OK (CPU image): %s\n",opts.input_path);
        else { char message[256]; jolt_diagnostic_format(&diagnostic,opts.input_path,message,sizeof(message)); fprintf(stderr,"%s\n",message); }
        jolt_image_program_destroy(image); tilly_container_free(library); tilly_container_free(source);
        return result==JOLT_OK ? 0 : 1;
    }
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diag = {.size = sizeof(diag)};
    jolt_status_t status = jolt_compile(source, &program, &diag);
    if (status != JOLT_OK) {
        char msg[256];
        jolt_diagnostic_format(&diag, opts.input_path, msg, sizeof(msg));
        fprintf(stderr, "%s\n", msg);
        tilly_container_free(source);
        return 1;
    }

    if (opts.dump_disasm) {
        size_t size = 0;
        const uint8_t *data = jolt_program_data(program, &size);
        dump_disasm(data, size);
    }

    if (opts.check_only) {
        printf("OK: %s\n", opts.input_path);
        jolt_program_destroy(program);
        tilly_container_free(source);
        return 0;
    }

    /* Default output path: input with .jbc extension */
    char output_buf[1024];
    if (!opts.output_path) {
        snprintf(output_buf, sizeof(output_buf), "%s.jbc", opts.input_path);
        opts.output_path = output_buf;
    }

    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    if (write_file(opts.output_path, data, size) != 0) {
        fprintf(stderr, "error: cannot write '%s'\n", opts.output_path);
        jolt_program_destroy(program);
        tilly_container_free(source);
        return 1;
    }

    printf("Compiled %s -> %s (%zu bytes)\n", opts.input_path, opts.output_path, size);
    jolt_program_destroy(program);
    tilly_container_free(source);
    return 0;
}
