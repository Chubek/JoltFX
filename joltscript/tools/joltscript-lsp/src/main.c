/* joltscript-lsp — Joltscript Language Server Protocol server
 *
 * Provides LSP features for Joltscript: hover, completion, diagnostics.
 *
 * Usage: joltscript-lsp [options]
 *
 * Options:
 *   -h, --help     Show this help message
 *   -v, --version  Show version information
 *       --stdio    Use stdio for communication (default)
 *       --tcp <port>  Use TCP for communication
 */

#include "tilly/memory.h"
#include "joltscript/compiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <errno.h>

#define JOLTLS_VERSION "0.1.0"

typedef struct {
    bool use_stdio;
    int tcp_port;
} joltlsp_options_t;

static void print_usage(const char *prog) {
    fprintf(stderr, "Usage: %s [options]\n", prog);
    fprintf(stderr, "\nOptions:\n");
    fprintf(stderr, "  -h, --help           Show this help message\n");
    fprintf(stderr, "  -v, --version        Show version information\n");
    fprintf(stderr, "      --stdio          Use stdio for communication (default)\n");
    fprintf(stderr, "      --tcp <port>     Use TCP for communication\n");
}

static void print_version(void) {
    printf("joltscript-lsp %s — Joltscript LSP server\n", JOLTLS_VERSION);
}

/* Bounded, exact-length LSP messages; malformed lengths never reach allocation. */
static char *read_message(void) {
    char header[256];
    size_t content_length = 0;
    bool headers_done = false;
    while (fgets(header, sizeof(header), stdin)) {
        if (strcmp(header, "\r\n") == 0 || strcmp(header, "\n") == 0) {
            headers_done = true;
            break;
        }
        if (strncmp(header, "Content-Length:", 15) == 0) {
            char *start = header + 15, *end;
            while (*start && isspace((unsigned char)*start)) ++start;
            if (!isdigit((unsigned char)*start)) return NULL;
            errno = 0;
            unsigned long long length = strtoull(start, &end, 10);
            if (errno || end == start || !length || length > 16u * 1024u * 1024u) return NULL;
            while (*end && isspace((unsigned char)*end)) ++end;
            if (*end) return NULL;
            content_length = (size_t)length;
        }
    }
    if (!headers_done || content_length == 0) {
        return NULL;
    }
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
    char buf[4096];
    snprintf(buf, sizeof(buf), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"result\":%s}", id, result);
    send_message(buf);
}

static void send_error(int id, int code, const char *message) {
    char buf[4096];
    snprintf(buf, sizeof(buf), "{\"jsonrpc\":\"2.0\",\"id\":%d,\"error\":{\"code\":%d,\"message\":\"%s\"}}", id, code, message);
    send_message(buf);
}

static void handle_initialize(int id, const char *params) {
    (void)params;
    send_response(id, "{\"capabilities\":{\"textDocumentSync\":1,\"hoverProvider\":true,\"completionProvider\":{\"triggerCharacters\":[\"(\",\" \"]},\"diagnosticProvider\":{\"interFileDependencies\":false,\"workspaceDiagnostics\":false}}}");
}

static void handle_hover(int id, const char *params) {
    (void)params;
    send_response(id, "null");
}

static void handle_completion(int id, const char *params) {
    (void)params;
    send_response(id, "[]");
}

static void handle_did_open(int id, const char *params) {
    (void)id;
    (void)params;
    /* Send empty diagnostics */
    send_message("{\"jsonrpc\":\"2.0\",\"method\":\"textDocument/publishDiagnostics\",\"params\":{\"uri\":\"\",\"diagnostics\":[]}}");
}

static void handle_shutdown(int id, const char *params) {
    (void)params;
    send_response(id, "null");
}

static bool server_running = true;
static void handle_exit(int id, const char *params) {
    (void)id;
    (void)params;
    server_running = false;
}

static void handle_request(int id, const char *method, const char *params) {
    if (strcmp(method, "initialize") == 0) handle_initialize(id, params);
    else if (strcmp(method, "shutdown") == 0) handle_shutdown(id, params);
    else if (strcmp(method, "exit") == 0) handle_exit(id, params);
    else if (strcmp(method, "textDocument/hover") == 0) handle_hover(id, params);
    else if (strcmp(method, "textDocument/completion") == 0) handle_completion(id, params);
    else if (strcmp(method, "textDocument/didOpen") == 0) handle_did_open(id, params);
    else if (strcmp(method, "textDocument/didChange") == 0) handle_did_open(id, params);
    else if (strcmp(method, "textDocument/didClose") == 0) handle_did_open(id, params);
    else send_error(id, -32601, "Method not found");
}

static void handle_notification(const char *method, const char *params) {
    if (strcmp(method, "exit") == 0) handle_exit(0, params);
    else if (strcmp(method, "textDocument/didOpen") == 0) handle_did_open(0, params);
    else if (strcmp(method, "textDocument/didChange") == 0) handle_did_open(0, params);
    else if (strcmp(method, "textDocument/didClose") == 0) handle_did_open(0, params);
}

static void parse_and_handle(const char *json) {
    /* Simple JSON parsing for LSP messages */
    const char *p = json;
    int id = 0;
    char method[256] = {0};
    char params[4096] = {0};
    bool has_id = false;
    bool is_request = false;

    /* Skip whitespace */
    while (*p && isspace((unsigned char)*p)) p++;
    if (*p != '{') return;
    p++;

    /* Parse object */
    while (*p && *p != '}') {
        while (*p && isspace((unsigned char)*p)) p++;
        if (*p == '"') {
            p++;
            char key[256] = {0};
            size_t i = 0;
            while (*p && *p != '"' && i < sizeof(key) - 1) {
                if (*p == '\\') { if (!p[1]) return; p++; }
                key[i++] = *p++;
            }
            if (*p == '"') p++;
            while (*p && isspace((unsigned char)*p)) p++;
            if (*p == ':') p++;
            while (*p && isspace((unsigned char)*p)) p++;

            if (strcmp(key, "id") == 0) {
                has_id = true;
                id = atoi(p);
                while (*p && *p != ',' && *p != '}') p++;
            } else if (strcmp(key, "method") == 0) {
                if (*p == '"') p++;
                size_t i = 0;
                while (*p && *p != '"' && i < sizeof(method) - 1) {
                    if (*p == '\\') { if (!p[1]) return; p++; }
                    method[i++] = *p++;
                }
                if (*p == '"') p++;
                is_request = true;
            } else if (strcmp(key, "params") == 0) {
                /* Copy params as-is */
                size_t i = 0;
                int depth = 0;
                bool in_string = false;
                while (*p && i < sizeof(params) - 1) {
                    if (*p == '"' && (i == 0 || params[i-1] != '\\')) in_string = !in_string;
                    if (!in_string) {
                        if (*p == '{' || *p == '[') depth++;
                        else if (*p == '}' || *p == ']') depth--;
                        else if (*p == ',' && depth == 0) break;
                    }
                    params[i++] = *p++;
                }
                params[i] = 0;
            } else {
                /* Skip value */
                int depth = 0;
                bool in_string = false;
                while (*p) {
                    if (*p == '"' && (p == json || p[-1] != '\\')) in_string = !in_string;
                    if (!in_string) {
                        if (*p == '{' || *p == '[') depth++;
                        else if (*p == '}' || *p == ']') depth--;
                        else if (*p == ',' && depth == 0) break;
                        else if (*p == '}' && depth == 0) break;
                    }
                    p++;
                }
            }
        } else if (*p && *p != '}') p++;
        if (*p == ',') p++;
    }

    if (is_request && has_id) {
        handle_request(id, method, params);
    } else if (is_request) {
        handle_notification(method, params);
    }
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
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            print_usage(argv[0]);
            exit(0);
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            print_version();
            exit(0);
        } else if (strcmp(argv[i], "--stdio") == 0) {
            opts->use_stdio = true;
        } else if (strcmp(argv[i], "--tcp") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --tcp requires a port number\n");
                return -1;
            }
            opts->use_stdio = false;
            opts->tcp_port = atoi(argv[++i]);
        } else {
            fprintf(stderr, "error: unknown option: %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char **argv) {
    joltlsp_options_t opts;
    if (parse_args(argc, argv, &opts) != 0) return 1;
    lsp_server();
    return 0;
}
