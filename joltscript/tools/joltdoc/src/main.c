/* joltdoc — Joltscript documentation generator
 *
 * Generates Markdown documentation from .jolt source files.
 *
 * Usage: joltdoc [options] <input.jolt> [-o <output.md>]
 *
 * Options:
 *   -o, --output <path>  Write documentation to <path>
 *   -h, --help           Show this help message
 *   -v, --version        Show version information
 *       --html           Generate HTML instead of Markdown
 *       --stdin          Read from stdin, write to stdout
 */

#include "tilly/memory.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define JOLTDOC_VERSION "0.1.0"

typedef struct {
    const char *input_path;
    const char *output_path;
    bool html_output;
    bool use_stdin;
} joltdoc_options_t;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input.jolt> [-o <output.md>]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o, --output <path>  Write documentation to <path>\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --version        Show version information\n");
    fprintf(stderr, "      --html           Generate HTML instead of Markdown\n");
    fprintf(stderr, "      --stdin          Read from stdin, write to stdout\n");
}

static void print_version(void) {
    printf("joltdoc %s — Joltscript documentation generator\n", JOLTDOC_VERSION);
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = tilly_mem_alloc((size_t)size + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t read = fread(buf, 1, (size_t)size, f);
    buf[read] = 0;
    fclose(f);
    if (out_size) *out_size = read;
    return buf;
}

static char *read_stdin(size_t *out_size) {
    size_t capacity = 4096;
    size_t size = 0;
    char *buf = tilly_mem_alloc(capacity);
    if (!buf) return NULL;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (size + 1 >= capacity) {
            if (capacity > SIZE_MAX / 2) { tilly_mem_free(buf); return NULL; }
            capacity *= 2;
            char *new_buf = tilly_mem_realloc(buf, capacity);
            if (!new_buf) { tilly_mem_free(buf); return NULL; }
            buf = new_buf;
        }
        buf[size++] = (char)c;
    }
    buf[size] = 0;
    if (out_size) *out_size = size;
    return buf;
}

static int write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    size_t len = strlen(content);
    size_t written = fwrite(content, 1, len, f);
    fclose(f);
    return written == len ? 0 : -1;
}

/* Extract documentation from source */
static char *extract_docs(const char *src, bool html) {
    /* Every emitted form has at most its source length plus 80 markup bytes;
     * the shortest recognized form has five source bytes.
     * Reserve a checked worst case including the fixed HTML header/footer. */
    if (strlen(src) > (SIZE_MAX - 4096) / 16) return NULL;
    size_t capacity = strlen(src) * 16 + 4096;
    size_t size = 0;
    char *out = tilly_mem_alloc(capacity);
    if (!out) return NULL;

    if (html) {
        size += sprintf(out + size, "<!DOCTYPE html>\n<html>\n<head>\n");
        size += sprintf(out + size, "<title>Joltscript Documentation</title>\n");
        size += sprintf(out + size, "<style>\n");
        size += sprintf(out + size, "body { font-family: sans-serif; max-width: 800px; margin: 0 auto; padding: 20px; }\n");
        size += sprintf(out + size, "h1 { color: #333; }\n");
        size += sprintf(out + size, "h2 { color: #666; border-bottom: 1px solid #ccc; padding-bottom: 5px; }\n");
        size += sprintf(out + size, "pre { background: #f5f5f5; padding: 10px; border-radius: 4px; }\n");
        size += sprintf(out + size, "code { background: #f5f5f5; padding: 2px 4px; border-radius: 3px; }\n");
        size += sprintf(out + size, ".kernel { margin: 20px 0; padding: 15px; border: 1px solid #ddd; border-radius: 4px; }\n");
        size += sprintf(out + size, ".param { color: #0066cc; }\n");
        size += sprintf(out + size, "</style>\n</head>\n<body>\n");
    } else {
        size += sprintf(out + size, "# Joltscript Documentation\n\n");
    }

    /* Parse source for defkernel, defn, defmacro, etc. */
    const char *p = src;
    while (*p) {
        /* Skip whitespace */
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Look for documentation comments */
        if (p[0] == ';' && p[1] == ';') {
            /* Documentation comment */
            const char *start = p;
            while (*p && *p != '\n') p++;
            size_t len = p - start;
            if (size + len + 16 > capacity) {
                capacity = (size + len + 16) * 2;
                char *new_out = tilly_mem_realloc(out, capacity);
                if (!new_out) { tilly_mem_free(out); return NULL; }
                out = new_out;
            }
            memcpy(out + size, start, len);
            size += len;
            out[size++] = '\n';
            continue;
        }

        /* Look for defkernel */
        if (strncmp(p, "(defkernel", 10) == 0) {
            const char *start = p;
            /* Find the name */
            p += 10;
            while (*p && isspace((unsigned char)*p)) p++;
            const char *name_start = p;
            while (*p && !isspace((unsigned char)*p) && *p != ')' && *p != '(') p++;
            size_t name_len = p - name_start;
            char *name = tilly_mem_strndup(name_start, name_len);
            if (!name) { tilly_mem_free(out); return NULL; }

            if (html) {
                size += sprintf(out + size, "<div class=\"kernel\">\n");
                size += sprintf(out + size, "<h2><code>%s</code></h2>\n", name);
            } else {
                size += sprintf(out + size, "## `%s`\n\n", name);
            }
            tilly_mem_free(name);

            /* Find the body */
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
            continue;
        }

        /* Look for defn */
        if (strncmp(p, "(defn", 5) == 0) {
            const char *start = p;
            p += 5;
            while (*p && isspace((unsigned char)*p)) p++;
            const char *name_start = p;
            while (*p && !isspace((unsigned char)*p) && *p != ')' && *p != '(' && *p != '[') p++;
            size_t name_len = p - name_start;
            char *name = tilly_mem_strndup(name_start, name_len);
            if (!name) { tilly_mem_free(out); return NULL; }

            if (html) {
                size += sprintf(out + size, "<h2><code>%s</code></h2>\n", name);
            } else {
                size += sprintf(out + size, "## `%s`\n\n", name);
            }
            tilly_mem_free(name);

            /* Find the body */
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
            continue;
        }

        /* Look for defmacro */
        if (strncmp(p, "(defmacro", 9) == 0) {
            const char *start = p;
            p += 9;
            while (*p && isspace((unsigned char)*p)) p++;
            const char *name_start = p;
            while (*p && !isspace((unsigned char)*p) && *p != ')' && *p != '(' && *p != '[') p++;
            size_t name_len = p - name_start;
            char *name = tilly_mem_strndup(name_start, name_len);
            if (!name) { tilly_mem_free(out); return NULL; }

            if (html) {
                size += sprintf(out + size, "<h2><code>%s</code> <em>(macro)</em></h2>\n", name);
            } else {
                size += sprintf(out + size, "## `%s` *(macro)*\n\n", name);
            }
            tilly_mem_free(name);

            /* Find the body */
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
            continue;
        }

        /* Look for defconst */
        if (strncmp(p, "(defconst", 9) == 0) {
            const char *start = p;
            p += 9;
            while (*p && isspace((unsigned char)*p)) p++;
            const char *name_start = p;
            while (*p && !isspace((unsigned char)*p) && *p != ')' && *p != '(') p++;
            size_t name_len = p - name_start;
            char *name = tilly_mem_strndup(name_start, name_len);
            if (!name) { tilly_mem_free(out); return NULL; }

            if (html) {
                size += sprintf(out + size, "<h2><code>%s</code> <em>(constant)</em></h2>\n", name);
            } else {
                size += sprintf(out + size, "## `%s` *(constant)*\n\n", name);
            }
            tilly_mem_free(name);

            /* Find the body */
            int depth = 1;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
            continue;
        }

        /* Skip other forms */
        if (*p == '(') {
            int depth = 1;
            p++;
            while (*p && depth > 0) {
                if (*p == '(') depth++;
                else if (*p == ')') depth--;
                p++;
            }
        } else {
            p++;
        }
    }

    if (html) {
        size += sprintf(out + size, "</body>\n</html>\n");
    }

    out[size] = 0;
    return out;
}

static int parse_args(int argc, char **argv, joltdoc_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(argv[i], "--html") == 0) {
            opts->html_output = true;
        } else if (strcmp(argv[i], "--stdin") == 0) {
            opts->use_stdin = true;
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
    }
    if (!opts->input_path && !opts->use_stdin) {
        fprintf(stderr, "error: no input file specified\n");
        print_usage(argv[0]);
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    joltdoc_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) return 1;

    size_t src_size = 0;
    char *source;
    if (opts.use_stdin) {
        source = read_stdin(&src_size);
    } else {
        source = read_file(opts.input_path, &src_size);
    }
    if (!source) {
        fprintf(stderr, "error: cannot read input\n");
        return 1;
    }

    char *docs = extract_docs(source, opts.html_output);
    if (!docs) {
        fprintf(stderr, "error: documentation generation failed\n");
        tilly_mem_free(source);
        return 1;
    }

    if (opts.use_stdin) {
        printf("%s", docs);
    } else if (opts.output_path) {
        if (write_file(opts.output_path, docs) != 0) {
            fprintf(stderr, "error: cannot write '%s'\n", opts.output_path);
            tilly_mem_free(source);
            tilly_mem_free(docs);
            return 1;
        }
        printf("Generated documentation %s -> %s\n", opts.input_path, opts.output_path);
    } else {
        printf("%s", docs);
    }

    tilly_mem_free(source);
    tilly_mem_free(docs);
    return 0;
}
