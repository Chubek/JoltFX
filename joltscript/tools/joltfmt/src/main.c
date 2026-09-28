/* joltfmt — Joltscript formatter
 *
 * Formats .jolt source files according to the Joltscript style guide.
 *
 * Usage: joltfmt [options] <input.jolt> [-o <output.jolt>]
 *
 * Options:
 *   -o, --output <path>  Write formatted output to <path>
 *   -h, --help           Show this help message
 *   -v, --version        Show version information
 *       --check          Check if file is formatted (exit 1 if not)
 *       --stdin          Read from stdin, write to stdout
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>

#define JOLTFMT_VERSION "0.1.0"
#define MAX_LINE_WIDTH 100
#define INDENT_SIZE 2

typedef struct {
    const char *input_path;
    const char *output_path;
    bool check_only;
    bool use_stdin;
} joltfmt_options_t;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options] <input.jolt> [-o <output.jolt>]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -o, --output <path>  Write formatted output to <path>\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --version        Show version information\n");
    fprintf(stderr, "      --check          Check if file is formatted (exit 1 if not)\n");
    fprintf(stderr, "      --stdin          Read from stdin, write to stdout\n");
}

static void print_version(void) {
    printf("joltfmt %s — Joltscript formatter\n", JOLTFMT_VERSION);
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size < 0) { fclose(f); return NULL; }
    char *buf = malloc((size_t)size + 1);
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
    char *buf = malloc(capacity);
    if (!buf) return NULL;
    int c;
    while ((c = fgetc(stdin)) != EOF) {
        if (size + 1 >= capacity) {
            capacity *= 2;
            char *new_buf = realloc(buf, capacity);
            if (!new_buf) { free(buf); return NULL; }
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

/* Token types for the formatter */
typedef enum {
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_SYMBOL,
    TOKEN_NUMBER,
    TOKEN_STRING,
    TOKEN_KEYWORD,
    TOKEN_COMMENT,
    TOKEN_EOF
} token_type_t;

typedef struct {
    token_type_t type;
    char *text;
    size_t len;
    size_t line;
    size_t col;
} token_t;

typedef struct {
    const char *src;
    size_t pos;
    size_t len;
    size_t line;
    size_t col;
} lexer_t;

static void lexer_init(lexer_t *lex, const char *src) {
    lex->src = src;
    lex->pos = 0;
    lex->len = strlen(src);
    lex->line = 1;
    lex->col = 1;
}

static void lexer_skip_ws(lexer_t *lex) {
    while (lex->pos < lex->len) {
        char c = lex->src[lex->pos];
        if (c == ' ' || c == '\t' || c == '\r') {
            lex->pos++;
            lex->col++;
        } else if (c == '\n') {
            lex->pos++;
            lex->line++;
            lex->col = 1;
        } else if (c == ';') {
            while (lex->pos < lex->len && lex->src[lex->pos] != '\n') {
                lex->pos++;
                lex->col++;
            }
        } else {
            break;
        }
    }
}

static token_t *lexer_next(lexer_t *lex) {
    lexer_skip_ws(lex);
    if (lex->pos >= lex->len) {
        token_t *tok = calloc(1, sizeof(token_t));
        tok->type = TOKEN_EOF;
        return tok;
    }
    char c = lex->src[lex->pos];
    token_t *tok = calloc(1, sizeof(token_t));
    tok->line = lex->line;
    tok->col = lex->col;
    if (c == '(') { tok->type = TOKEN_LPAREN; tok->text = strdup("("); tok->len = 1; lex->pos++; lex->col++; }
    else if (c == ')') { tok->type = TOKEN_RPAREN; tok->text = strdup(")"); tok->len = 1; lex->pos++; lex->col++; }
    else if (c == '[') { tok->type = TOKEN_LBRACKET; tok->text = strdup("["); tok->len = 1; lex->pos++; lex->col++; }
    else if (c == ']') { tok->type = TOKEN_RBRACKET; tok->text = strdup("]"); tok->len = 1; lex->pos++; lex->col++; }
    else if (c == '{') { tok->type = TOKEN_LBRACE; tok->text = strdup("{"); tok->len = 1; lex->pos++; lex->col++; }
    else if (c == '}') { tok->type = TOKEN_RBRACE; tok->text = strdup("}"); tok->len = 1; lex->pos++; lex->col++; }
    else if (c == '"') {
        size_t start = lex->pos;
        lex->pos++; lex->col++;
        while (lex->pos < lex->len && lex->src[lex->pos] != '"') {
            if (lex->src[lex->pos] == '\\') { lex->pos++; lex->col++; }
            lex->pos++; lex->col++;
        }
        if (lex->pos < lex->len) { lex->pos++; lex->col++; }
        tok->type = TOKEN_STRING;
        tok->len = lex->pos - start;
        tok->text = strndup(lex->src + start, tok->len);
    }
    else if (c == ':') {
        size_t start = lex->pos;
        lex->pos++; lex->col++;
        while (lex->pos < lex->len && !isspace((unsigned char)lex->src[lex->pos]) &&
               lex->src[lex->pos] != '(' && lex->src[lex->pos] != ')' &&
               lex->src[lex->pos] != '[' && lex->src[lex->pos] != ']' &&
               lex->src[lex->pos] != '{' && lex->src[lex->pos] != '}' &&
               lex->src[lex->pos] != ';') {
            lex->pos++; lex->col++;
        }
        tok->type = TOKEN_KEYWORD;
        tok->len = lex->pos - start;
        tok->text = strndup(lex->src + start, tok->len);
    }
    else if (c == ';') {
        size_t start = lex->pos;
        while (lex->pos < lex->len && lex->src[lex->pos] != '\n') {
            lex->pos++; lex->col++;
        }
        tok->type = TOKEN_COMMENT;
        tok->len = lex->pos - start;
        tok->text = strndup(lex->src + start, tok->len);
    }
    else {
        size_t start = lex->pos;
        while (lex->pos < lex->len && !isspace((unsigned char)lex->src[lex->pos]) &&
               lex->src[lex->pos] != '(' && lex->src[lex->pos] != ')' &&
               lex->src[lex->pos] != '[' && lex->src[lex->pos] != ']' &&
               lex->src[lex->pos] != '{' && lex->src[lex->pos] != '}' &&
               lex->src[lex->pos] != ';') {
            lex->pos++; lex->col++;
        }
        tok->len = lex->pos - start;
        tok->text = strndup(lex->src + start, tok->len);
        /* Check if it's a number */
        char *end;
        strtod(tok->text, &end);
        if (*end == 0 && tok->len > 0) {
            tok->type = TOKEN_NUMBER;
        } else {
            tok->type = TOKEN_SYMBOL;
        }
    }
    return tok;
}

static void token_free(token_t *tok) {
    if (tok) { free(tok->text); free(tok); }
}

/* Simple formatter: re-indent with 2-space indentation */
static char *format_source(const char *src) {
    lexer_t lex;
    lexer_init(&lex, lex.src = src);
    size_t capacity = strlen(src) * 2;
    size_t size = 0;
    char *out = malloc(capacity);
    if (!out) return NULL;
    int indent = 0;
    bool at_line_start = true;
    token_t *tok;
    while ((tok = lexer_next(&lex))->type != TOKEN_EOF) {
        if (size + tok->len + 16 > capacity) {
            capacity = (size + tok->len + 16) * 2;
            char *new_out = realloc(out, capacity);
            if (!new_out) { free(out); token_free(tok); return NULL; }
            out = new_out;
        }
        if (tok->type == TOKEN_LPAREN || tok->type == TOKEN_LBRACKET || tok->type == TOKEN_LBRACE) {
            if (!at_line_start) { out[size++] = ' '; }
            memcpy(out + size, tok->text, tok->len); size += tok->len;
            indent++;
            at_line_start = false;
        } else if (tok->type == TOKEN_RPAREN || tok->type == TOKEN_RBRACKET || tok->type == TOKEN_RBRACE) {
            indent--;
            memcpy(out + size, tok->text, tok->len); size += tok->len;
            at_line_start = false;
        } else if (tok->type == TOKEN_COMMENT) {
            if (!at_line_start) { out[size++] = ' '; }
            memcpy(out + size, tok->text, tok->len); size += tok->len;
            at_line_start = false;
        } else {
            if (at_line_start) {
                for (int i = 0; i < indent * INDENT_SIZE; ++i) out[size++] = ' ';
                at_line_start = false;
            } else {
                out[size++] = ' ';
            }
            memcpy(out + size, tok->text, tok->len); size += tok->len;
        }
        token_free(tok);
    }
    token_free(tok);
    out[size++] = '\n';
    out[size] = 0;
    return out;
}

static int parse_args(int argc, char **argv, joltfmt_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(argv[i], "--check") == 0) {
            opts->check_only = true;
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
    joltfmt_options_t opts;
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

    char *formatted = format_source(source);
    if (!formatted) {
        fprintf(stderr, "error: formatting failed\n");
        free(source);
        return 1;
    }

    if (opts.check_only) {
        if (strcmp(source, formatted) != 0) {
            fprintf(stderr, "error: file is not formatted\n");
            free(source);
            free(formatted);
            return 1;
        }
        printf("OK\n");
        free(source);
        free(formatted);
        return 0;
    }

    if (opts.use_stdin) {
        printf("%s", formatted);
    } else if (opts.output_path) {
        if (write_file(opts.output_path, formatted) != 0) {
            fprintf(stderr, "error: cannot write '%s'\n", opts.output_path);
            free(source);
            free(formatted);
            return 1;
        }
        printf("Formatted %s -> %s\n", opts.input_path, opts.output_path);
    } else {
        printf("%s", formatted);
    }

    free(source);
    free(formatted);
    return 0;
}
