/* joltscript-lsp — Joltscript Language Server Protocol server
 *
 * Stdio-only, bounded LSP server for the shipped Joltscript profiles. It uses
 * jolt_compile for diagnostics so editor feedback follows the compiler and typed
 * VM semantic checker instead of a parallel parser.
 */

#include "tilly/memory.h"
#include "joltscript/compiler.h"
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JOLTLS_VERSION "0.2.0"
#define JOLTLS_MAX_MESSAGE (16u * 1024u * 1024u)

typedef struct {
    bool use_stdio;
    int tcp_port;
} joltlsp_options_t;

static char *current_uri;
static char *current_text;
static bool server_running = true;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --version        Show version information\n");
    fprintf(stderr, "      --stdio          Use stdio for communication (default)\n");
    fprintf(stderr, "      --tcp <port>     Reserved; stdio is currently implemented\n");
}

static void print_version(void) {
    printf("joltscript-lsp %s — Joltscript LSP server\n", JOLTLS_VERSION);
}

static char *read_message(void) {
    char header[256];
    size_t content_length = 0;
    bool headers_done = false;
    while (fgets(header, sizeof(header), stdin)) {
        if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) { headers_done = true; break; }
        if (strncmp(header, "Content-Length:", 15) == 0) {
            char *start = header + 15, *end;
            while (*start && isspace((unsigned char)*start)) ++start;
            if (!isdigit((unsigned char)*start)) return NULL;
            errno = 0;
            unsigned long long length = strtoull(start, &end, 10);
            if (errno || end == start || !length || length > JOLTLS_MAX_MESSAGE) return NULL;
            while (*end && isspace((unsigned char)*end)) ++end;
            if (*end) return NULL;
            content_length = (size_t)length;
        }
    }
    if (!headers_done || content_length == 0) return NULL;
    char *body = tilly_mem_alloc(content_length + 1);
    if (!body) return NULL;
    size_t read = fread(body, 1, content_length, stdin);
    if (read != content_length) { tilly_mem_free(body); return NULL; }
    body[read] = 0;
    return body;
}

static void send_message(const char *json) {
    printf("Content-Length: %zu\r\n\r\n%s", strlen(json), json);
    fflush(stdout);
}

static void send_response(int id, const char *result) {
    char buf[65536];
    snprintf(buf, sizeof(buf), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result);
    send_message(buf);
}

static void send_error(int id, int code, const char *message) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":%d,\"message\":\"%s\"}}", id, code, message);
    send_message(buf);
}

static char *json_escape_alloc(const char *text) {
    size_t len = text ? strlen(text) : 0;
    if (len > (SIZE_MAX - 1) / 6) return NULL;
    char *out = tilly_mem_alloc(len * 6 + 1);
    if (!out) return NULL;
    size_t o = 0;
    for (size_t i = 0; i < len; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
        else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
        else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
        else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
        else if (c < 32) o += (size_t)sprintf(out + o, "\\u%04x", c);
        else out[o++] = (char)c;
    }
    out[o] = 0;
    return out;
}

static const char *find_key_value(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && isspace((unsigned char)*p)) ++p;
    if (*p != ':') return NULL;
    ++p;
    while (*p && isspace((unsigned char)*p)) ++p;
    return p;
}

static char *json_string_at(const char *p) {
    if (!p || *p != '"') return NULL;
    ++p;
    size_t capacity = strlen(p) + 1, size = 0;
    char *out = tilly_mem_alloc(capacity);
    if (!out) return NULL;
    while (*p && *p != '"') {
        char c = *p++;
        if (c == '\\') {
            c = *p++;
            if (!c) { tilly_mem_free(out); return NULL; }
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'u') {
                for (int i = 0; i < 4 && isxdigit((unsigned char)*p); ++i) ++p;
                c = '?';
            }
        }
        out[size++] = c;
    }
    if (*p != '"') { tilly_mem_free(out); return NULL; }
    out[size] = 0;
    return out;
}

static char *json_get_string(const char *json, const char *key) {
    return json_string_at(find_key_value(json, key));
}

static int json_get_int(const char *json, const char *key, int fallback) {
    const char *p = find_key_value(json, key);
    if (!p) return fallback;
    return atoi(p);
}

static void publish_diagnostics(const char *uri, const char *text) {
    jolt_program_t *program = NULL;
    jolt_diagnostic_t diagnostic = {.size = sizeof(diagnostic)};
    jolt_status_t status = jolt_compile(text ? text : "", &program, &diagnostic);
    char *escaped_uri = json_escape_alloc(uri ? uri : "");
    if (!escaped_uri) return;
    if (status == JOLT_OK) {
        char buf[2048];
        snprintf(buf, sizeof(buf), "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"diagnostics\":[]}}", escaped_uri);
        send_message(buf);
        jolt_program_destroy(program);
        tilly_mem_free(escaped_uri);
        return;
    }
    char message[256];
    jolt_diagnostic_format(&diagnostic, uri ? uri : "<document>", message, sizeof(message));
    char *escaped_msg = json_escape_alloc(message);
    if (!escaped_msg) { tilly_mem_free(escaped_uri); return; }
    size_t line = diagnostic.line ? diagnostic.line - 1 : 0;
    size_t column = diagnostic.column ? diagnostic.column - 1 : 0;
    char buf[4096];
    snprintf(buf, sizeof(buf),
             "{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"%s\",\"diagnostics\":[{\"range\":{\"start\":{\"line\":%zu,\"character\":%zu},\"end\":{\"line\":%zu,\"character\":%zu}},\"severity\":1,\"source\":\"joltscript\",\"message\":\"%s\"}]}}}",
             escaped_uri, line, column, line, column + 1, escaped_msg);
    send_message(buf);
    tilly_mem_free(escaped_msg);
    tilly_mem_free(escaped_uri);
}

static void replace_current_document(const char *json) {
    char *uri = json_get_string(json, "uri");
    char *text = json_get_string(json, "text");
    if (!text) text = tilly_mem_strdup("");
    if (!uri) uri = tilly_mem_strdup("");
    if (!uri || !text) { tilly_mem_free(uri); tilly_mem_free(text); return; }
    tilly_mem_free(current_uri);
    tilly_mem_free(current_text);
    current_uri = uri;
    current_text = text;
    publish_diagnostics(current_uri, current_text);
}

static bool word_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '?' || c == '!' || c == '.' || c == '/' || c == '<' || c == '>' || c == '=' || c == '*' || c == '+';
}

static char *word_at_position(const char *text, int line, int character) {
    if (!text || line < 0 || character < 0) return NULL;
    const char *p = text;
    for (int l = 0; l < line && *p; ++p) if (*p == '\n') ++l;
    const char *line_start = p;
    for (int c = 0; c < character && *p && *p != '\n'; ++c) ++p;
    const char *start = p, *end = p;
    while (start > line_start && word_char(start[-1])) --start;
    while (*end && *end != '\n' && word_char(*end)) ++end;
    if (end <= start) return NULL;
    return tilly_mem_strndup(start, (size_t)(end - start));
}

typedef struct { const char *name; const char *detail; } symbol_doc_t;
static const symbol_doc_t docs[] = {
    {"defkernel", "Defines a Joltscript kernel: (defkernel name [inputs] body)."},
    {"defn", "Defines a typed helper function available to the kernel."},
    {"fn", "Creates a lexical closure."},
    {"let", "Creates an immutable binding; typed profile accepts annotations."},
    {"var", "Creates a mutable binding for set!."},
    {"set!", "Assigns a mutable var binding."},
    {"if", "Lazy conditional; only the selected branch executes."},
    {"cond", "Checks clauses in order and returns the first matching result."},
    {"for", "Typed end-exclusive integer range loop."},
    {"while", "Typed pre-test loop with VM step budget enforcement."},
    {"loop", "Typed loop that exits with break."},
    {"import", "Imports one of the six built-in standard-library modules."},
    {"jolt.math", "Standard math module: trig, interpolation, random, linear algebra."},
    {"jolt.graphics", "Color transforms, blend modes and sample filters."},
    {"jolt.geometry", "Vectors, matrices, quaternions, splines and bounds."},
    {"jolt.time", "Frame/time conversion, easing, curves and looping."},
    {"jolt.collections", "Immutable collections, sequences and higher-order functions."},
    {"jolt.strings", "UTF-8 string utilities, formatting and PCRE2 regex helpers."},
    {"sin", "jolt.math intrinsic: sine in radians."},
    {"cos", "jolt.math intrinsic: cosine in radians."},
    {"reduce", "jolt.collections intrinsic: reduce a sequence with a callback."},
    {"parse-float", "jolt.strings intrinsic: parse a complete finite float string."}
};

static const char *lookup_doc(const char *word) {
    if (!word) return NULL;
    for (size_t i = 0; i < sizeof(docs) / sizeof(*docs); ++i)
        if (strcmp(word, docs[i].name) == 0) return docs[i].detail;
    return NULL;
}

static void handle_initialize(int id) {
    send_response(id, "{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\"(\",\" \",\".\"]},\"diagnosticProvider\":{\"interFileDependencies\":false,\"workspaceDiagnostics\":false}}}");
}

static void handle_hover(int id, const char *json) {
    int line = json_get_int(json, "line", 0);
    int character = json_get_int(json, "character", 0);
    char *word = word_at_position(current_text, line, character);
    const char *detail = lookup_doc(word);
    if (!detail) { send_response(id, "null"); tilly_mem_free(word); return; }
    char *escaped = json_escape_alloc(detail);
    char *escaped_word = json_escape_alloc(word);
    if (!escaped || !escaped_word) { tilly_mem_free(escaped); tilly_mem_free(escaped_word); tilly_mem_free(word); send_response(id, "null"); return; }
    char result[2048];
    snprintf(result, sizeof(result), "{\"contents\":{\"kind\":\"markdown\",\"value\":\"`%s`\\n\\n%s\"}}", escaped_word, escaped);
    send_response(id, result);
    tilly_mem_free(escaped);
    tilly_mem_free(escaped_word);
    tilly_mem_free(word);
}

static void handle_completion(int id) {
    char result[32768];
    size_t size = 0;
    result[size++] = '[';
    for (size_t i = 0; i < sizeof(docs) / sizeof(*docs); ++i) {
        if (i) result[size++] = ',';
        size += (size_t)snprintf(result + size, sizeof(result) - size,
            "{\"label\":\"%s\",\"kind\":%d,\"detail\":\"Joltscript\"}",
            docs[i].name, strstr(docs[i].name, "jolt.") == docs[i].name ? 9 : 3);
        if (size >= sizeof(result) - 256) break;
    }
    result[size++] = ']';
    result[size] = 0;
    send_response(id, result);
}

static void handle_request(int id, const char *method, const char *json) {
    if (strcmp(method, "initialize") == 0) handle_initialize(id);
    else if (strcmp(method, "shutdown") == 0) send_response(id, "null");
    else if (strcmp(method, "textDocument/hover") == 0) handle_hover(id, json);
    else if (strcmp(method, "textDocument/completion") == 0) handle_completion(id);
    else if (strcmp(method, "textDocument/didOpen") == 0 || strcmp(method, "textDocument/didChange") == 0) { replace_current_document(json); if (id) send_response(id, "null"); }
    else send_error(id, -32601, "Method not found");
}

static void handle_notification(const char *method, const char *json) {
    if (strcmp(method, "exit") == 0) server_running = false;
    else if (strcmp(method, "textDocument/didOpen") == 0 || strcmp(method, "textDocument/didChange") == 0) replace_current_document(json);
    else if (strcmp(method, "textDocument/didClose") == 0 && current_uri) publish_diagnostics(current_uri, "(defkernel closed [] 0)");
}

static void parse_and_handle(const char *json) {
    char *method = json_get_string(json, "method");
    if (!method) return;
    const char *id_value = find_key_value(json, "id");
    bool has_id = id_value && *id_value != 'n';
    int id = has_id ? atoi(id_value) : 0;
    if (has_id) handle_request(id, method, json);
    else handle_notification(method, json);
    tilly_mem_free(method);
}

static void lsp_server(void) {
    while (server_running) {
        char *msg = read_message();
        if (!msg) break;
        parse_and_handle(msg);
        tilly_mem_free(msg);
    }
}

static int parse_args(int argc, char **argv, joltlsp_options_t *opts) {
    memset(opts, 0, sizeof(*opts));
    opts->use_stdio = true;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) { print_usage(argv[0]); exit(0); }
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) { print_version(); exit(0); }
        if (strcmp(argv[i], "--stdio") == 0) opts->use_stdio = true;
        else if (strcmp(argv[i], "--tcp") == 0) {
            if (i + 1 >= argc) { fprintf(stderr, "error: --tcp requires a port number\n"); return -1; }
            opts->use_stdio = false;
            opts->tcp_port = atoi(argv[++i]);
        } else { fprintf(stderr, "error: unknown option: %s\n", argv[i]); return -1; }
    }
    return 0;
}

int main(int argc, char **argv) {
    joltlsp_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) return 1;
    if (!opts.use_stdio) fprintf(stderr, "warning: --tcp is reserved; using stdio\n");
    lsp_server();
    tilly_mem_free(current_uri);
    tilly_mem_free(current_text);
    return 0;
}
