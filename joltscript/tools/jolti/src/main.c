/* jolti — Joltscript REPL
 *
 * Interactive Read-Eval-Print Loop for Joltscript.
 *
 * Usage: jolti [options]
 *
 * Options:
 *   -h, --help     Show this help message
 *   -v, --version  Show version information
 *   -e, --eval <expr>  Evaluate expression and exit
 */

#include "tilly/memory.h"
#include "joltscript/compiler.h"
#include "joltscript/value.h"
#include <inttypes.h>
#include "joltscript/vm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define JOLTI_VERSION "0.1.0"
#define JOLTI_PROMPT "jolt> "
#define JOLTI_CONTINUE_PROMPT "..> "

static void print_version(void) {
    printf("jolti %s — Joltscript REPL\n", JOLTI_VERSION);
}

static void print_help(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --version        Show version information\n");
    fprintf(stderr, "  -e, --eval <source>  Evaluate a complete kernel and exit (zero-initialized inputs)\n");
    fprintf(stderr, "\nExamples:\n");
    fprintf(stderr, "  %s\n", prog);
    fprintf(stderr, "  %s -e '(defkernel k [] (+ 1 2))'\n", prog);
    fprintf(stderr, "  %s -e '(import jolt.math) (defkernel k [] (sin HALF-PI))'\n", prog);
    fprintf(stderr, "\nPrints exact typed primitives and collections; each evaluation is independent.\n");
}

static void print_result(const float *outputs, size_t count) {
    if (count == 1) {
        printf("%g\n", outputs[0]);
    } else if (count == 4) {
        printf("rgba(%g, %g, %g, %g)\n", outputs[0], outputs[1], outputs[2], outputs[3]);
    } else {
        printf("(");
        for (size_t i = 0; i < count; ++i) {
            if (i > 0) printf(" ");
            printf("%g", outputs[i]);
        }
        printf(")\n");
    }
}

static void print_value(jolt_value_t v) {
    switch(v.type) {
    case JOLT_TYPE_VOID:printf("()");break;
    case JOLT_TYPE_I32:case JOLT_TYPE_I64:printf("%" PRId64,v.as.integer);break;
    case JOLT_TYPE_F32:case JOLT_TYPE_F64:printf("%.17g",v.as.number);break;
    case JOLT_TYPE_BOOL:printf("%s",v.as.unsigned_integer?"true":"false");break;
    case JOLT_TYPE_CHAR:printf("\\u{%" PRIX64 "}",v.as.unsigned_integer);break;
    case JOLT_TYPE_STRING:if(v.as.string.size) fwrite(v.as.string.data,1,v.as.string.size,stdout);break;
    case JOLT_TYPE_LIST:case JOLT_TYPE_VECTOR:case JOLT_TYPE_MAP:case JOLT_TYPE_SET:
        printf("%s",v.type==JOLT_TYPE_LIST?"(list ":v.type==JOLT_TYPE_MAP?"(hash-map ":v.type==JOLT_TYPE_SET?"(set ":"[");
        for(size_t i=0;i<v.as.collection.size;++i) {if(i) printf(" ");print_value(v.as.collection.items[i]);}
        printf("%s",v.type==JOLT_TYPE_VECTOR?"]":")");break;
    default:printf("%" PRIu64,v.as.unsigned_integer);break;
    }
}

static int eval_source(const char *source, const char *name) {
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diag = {.size = sizeof(diag)};
    jolt_status_t status = jolt_compile(source, &program, &diag);
    if (status != JOLT_OK) {
        char msg[256];
        jolt_diagnostic_format(&diag, name, msg, sizeof(msg));
        fprintf(stderr, "%s\n", msg);
        return 1;
    }

    size_t size = 0;
    const uint8_t *data = jolt_program_data(program, &size);
    uint32_t inputs = (uint32_t)data[8] | ((uint32_t)data[9] << 8) |
                      ((uint32_t)data[10] << 16) | ((uint32_t)data[11] << 24);
    uint32_t outputs = (uint32_t)data[12] | ((uint32_t)data[13] << 8) |
                       ((uint32_t)data[14] << 16) | ((uint32_t)data[15] << 24);

    jolt_vm_t *vm = jolt_vm_create();
    if (!vm) {
        fprintf(stderr, "error: cannot create VM\n");
        jolt_program_destroy(program);
        return 1;
    }

    if(data[4]==2) {
        jolt_type_t it[32],ot[4];jolt_value_t in[32]={{0}},out[4];
        status=jolt_bytecode_signature(data,size,it,32,ot,4);
        if(!status) { for(uint32_t i=0;i<inputs;++i) in[i].type=it[i];
            status=jolt_vm_run_values(vm,data,size,in,inputs,out,outputs,NULL); }
        if(status) fprintf(stderr,"error: execution failed with status %d\n",status);
        else for(uint32_t i=0;i<outputs;++i) {
            print_value(out[i]);
            printf("%s",i+1==outputs?"\n":" ");
        }
        jolt_vm_destroy(vm);jolt_program_destroy(program);return status?1:0;
    }
    float *in = tilly_mem_calloc(inputs ? inputs : 1, sizeof(float));
    float *out = tilly_mem_calloc(outputs ? outputs : 1, sizeof(float));
    if (!in || !out) {
        fprintf(stderr, "error: out of memory\n");
        tilly_mem_free(in); tilly_mem_free(out);
        jolt_vm_destroy(vm);
        jolt_program_destroy(program);
        return 1;
    }

    status = jolt_vm_run(vm, data, size, in, inputs, out, outputs);
    if (status != JOLT_OK) {
        fprintf(stderr, "error: execution failed with status %d\n", status);
        tilly_mem_free(in); tilly_mem_free(out);
        jolt_vm_destroy(vm);
        jolt_program_destroy(program);
        return 1;
    }

    print_result(out, outputs);
    tilly_mem_free(in); tilly_mem_free(out);
    jolt_vm_destroy(vm);
    jolt_program_destroy(program);
    return 0;
}

static void repl(void) {
    char line[4096];
    printf("Joltscript REPL %s\n", JOLTI_VERSION);
    printf("Type 'quit' or press Ctrl+D to exit.\n\n");

    for (;;) {
        printf("%s", JOLTI_PROMPT);
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        /* Strip trailing newline */
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n') line[len-1] = 0;

        /* Skip empty lines */
        bool empty = true;
        for (size_t i = 0; line[i]; ++i) {
            if (line[i] != ' ' && line[i] != '\t') { empty = false; break; }
        }
        if (empty) continue;

        /* Check for quit */
        if (strcmp(line, "quit") == 0 || strcmp(line, "exit") == 0) break;

        eval_source(line, "<repl>");
    }
}

int main(int argc, char **argv) {
    const char *eval_expr = NULL;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_help(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            return 0;
        } else if (strcmp(argv[i], "-e") == 0 || strcmp(argv[i], "--eval") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: %s requires an argument\n", argv[i]);
                return 1;
            }
            eval_expr = argv[++i];
        } else {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    if (eval_expr) {
        return eval_source(eval_expr, "<eval>");
    }

    repl();
    return 0;
}
