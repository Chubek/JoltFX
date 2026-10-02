/* Project interchange: the plain-text form of a graph and a sequence.
 *
 * See jfx_project.h for the format. The parser is strict about what it does not
 * understand - an unknown key or a bad value is an error naming the line - so a
 * mistyped project is reported rather than rendering as something else. */

#include "jfx/jfx_project.h"
#include "jfx/jfx_audio.h"
#include "tilly/attributes.h"

#include <stdarg.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TILLY_PRINTF_LIKE(3, 4)
static void set_error(char *out_error, size_t out_error_size, const char *fmt, ...) {
    if (!out_error || !out_error_size) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    vsnprintf(out_error, out_error_size, fmt, args);
    va_end(args);
}

/* ---- Line scanning ------------------------------------------------------- */

typedef struct {
    const char *cursor;
    const char *end;
    size_t line;
} reader_t;

static void line_begin(reader_t *reader) {
    while (reader->cursor < reader->end) {
        while (reader->cursor < reader->end && *reader->cursor != '\n') {
            reader->cursor++;
        }
        if (reader->cursor < reader->end) {
            reader->cursor++;
        }
        reader->line++;
        return;
    }
}

/* Reads the next significant line into `out`, stripping comments and trailing
 * space. Returns false at end of input. */
static bool next_line(reader_t *reader, char *out, size_t out_size) {
    while (reader->cursor < reader->end) {
        const char *finish = reader->cursor;
        bool quoted = false;
        while (finish < reader->end && *finish != '\n') {
            if (quoted && *finish == '\\' && finish + 1 < reader->end && finish[1] != '\n') {
                finish += 2;
                continue;
            }
            if (*finish == '"') quoted = !quoted;
            if (*finish == '#' && !quoted) break;
            ++finish;
        }
        if (quoted) {
            snprintf(out,out_size,"!unterminated-quoted-token"); line_begin(reader); return true;
        }
        size_t n = 0;
        while (reader->cursor + n < finish && (reader->cursor[n] == ' ' || reader->cursor[n] == '\t' ||
                   reader->cursor[n] == '\r')) {
            n++;
        }
        size_t length = (size_t)(finish - reader->cursor) - n;
        while (length > 0u && (reader->cursor[n + length - 1u] == ' ' ||
                   reader->cursor[n + length - 1u] == '\t' || reader->cursor[n + length - 1u] == '\r')) {
            length--;
        }
        if (length > 0u) {
            if (length + 1u > out_size) {
                snprintf(out,out_size,"!oversized-line"); line_begin(reader); return true;
            }
            memcpy(out, reader->cursor + n, length);
            out[length] = '\0';
            line_begin(reader);
            return true;
        }
        line_begin(reader);
    }
    return false;
}

/* ---- Token scanning ------------------------------------------------------ */

static const char *token(const char *cursor, const char *end, char *out, size_t out_size) {
    while (cursor < end && (*cursor == ' ' || *cursor == '\t')) {
        cursor++;
    }
    size_t n = 0;
    if (cursor < end && *cursor == '"') {
        ++cursor;
        while (cursor < end && *cursor != '"') {
            char c = *cursor++;
            if (c == '\\' && cursor < end) {
                c = *cursor++;
                if (c == 'n') c = '\n';
                else if (c == 'r') c = '\r';
                else if (c == 't') c = '\t';
            }
            if (n + 1 < out_size) out[n++] = c;
        }
        out[n] = '\0';
        return cursor < end ? cursor + 1 : cursor;
    }
    while (cursor + n < end && cursor[n] != ' ' && cursor[n] != '\t') {
        n++;
    }
    if (n + 1u > out_size) {
        n = out_size - 1u;
    }
    memcpy(out, cursor, n);
    out[n] = '\0';
    return cursor + n;
}

static bool read_float(const char **cursor, const char *end, float *out) {
    char word[64];
    *cursor = token(*cursor, end, word, sizeof(word));
    if (!word[0]) {
        return false;
    }
    char *tail = NULL;
    const double value = strtod(word, &tail);
    if (tail == word || *tail != '\0' || !isfinite(value) || !isfinite((float)value)) {
        return false;
    }
    *out = (float)value;
    return true;
}

static bool read_frame(const char **cursor, const char *end, uint64_t *out) {
    char word[64]; *cursor=token(*cursor,end,word,sizeof(word));
    if (!word[0] || word[0]=='-') return false;
    errno=0; char *tail=NULL; unsigned long long value=strtoull(word,&tail,10);
    if (errno || tail==word || *tail || value>INT64_MAX) return false;
    *out=(uint64_t)value;
    return true;
}
static bool read_uint(const char **cursor, const char *end, uint32_t *out) {
    uint64_t value;
    if (!read_frame(cursor,end,&value) || value>UINT32_MAX) return false;
    *out=(uint32_t)value; return true;
}
static bool read_signed_frame(const char **cursor,const char *end,int64_t *out) {
    char word[64]; *cursor=token(*cursor,end,word,sizeof(word));
    errno=0; char *tail=NULL; long long value=strtoll(word,&tail,10);
    if (errno || tail==word || *tail || value==INT64_MIN) return false;
    *out=(int64_t)value; return true;
}

static bool read_word(const char **cursor, const char *end, char *out, size_t out_size) {
    *cursor = token(*cursor, end, out, out_size);
    return out[0] != '\0';
}

static bool append(char *out_text, size_t out_size, size_t *used, const char *format, ...) TILLY_PRINTF_LIKE(4, 5);
static bool append_token(char *out, size_t cap, size_t *used, const char *text);

/* Graph labels/paths may be quoted or consume the rest of a legacy line. Unlike
 * the positional scanner, this rejects overflow instead of truncating a name. */
static bool graph_text(const char **cursor,const char *end,char *out,size_t cap) {
    const char *p=*cursor; while (p<end && (*p==' ' || *p=='\t')) ++p;
    bool quoted=p<end && *p=='"'; if (quoted) ++p;
    size_t n=0;
    while (p<end && (!quoted || *p!='"')) {
        char c=*p++;
        if (quoted && c=='\\') {
            if (p==end) return false;
            c=*p++; if (c=='n') c='\n'; else if (c=='r') c='\r'; else if (c=='t') c='\t';
        }
        if (n+1>=cap) return false;
        out[n++]=c;
    }
    if (quoted) { if (p==end) return false; ++p; }
    out[n]=0; *cursor=p; return true;
}

/* ---- Graphs -------------------------------------------------------------- */

jfx_result_t jfx_project_load_graph(const char *text, size_t length, jfx_graph_t **out_graph,
    uint32_t *out_output, uint32_t *out_width, uint32_t *out_height, char *out_error,
    size_t out_error_size) {
    if (!text || !out_graph || !out_output || !out_width || !out_height) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!length) {
        length = strlen(text);
    }
    if (length > JFX_PROJECT_MAX_BYTES) {
        set_error(out_error, out_error_size, "document is %zu bytes, over the %u byte limit",
            length, (unsigned)JFX_PROJECT_MAX_BYTES);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    const char *limit = text + length;
    jfx_graph_t *graph = jfx_graph_create();
    if (!graph) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    uint32_t width = 0, height = 0;
    uint32_t last = UINT32_MAX, output = UINT32_MAX;
    bool have_node = false;
    bool saw_output = false;
    bool explicit_graph = false;
    reader_t reader = { text, limit, 0 };
    char line[4096];
    while (next_line(&reader, line, sizeof(line))) {
        const char *cursor = line;
        char key[64];
        if (!read_word(&cursor, line + strlen(line), key, sizeof(key))) {
            continue;
        }
        const char *rest_end = line + strlen(line);
        if (!strcmp(key,"graph")) {
            explicit_graph=true;
        } else if (strcmp(key, "size") == 0) {
            if (!read_uint(&cursor, rest_end, &width) || !read_uint(&cursor, rest_end, &height) ||
                !width || !height) {
                set_error(out_error, out_error_size, "line %zu: size needs two positive numbers",
                    reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        } else if (strcmp(key, "node") == 0) {
            char kind[64];
            char label[JFX_NODE_LABEL_MAX]={0};
            if (!read_word(&cursor, rest_end, kind, sizeof(kind))) {
                set_error(out_error, out_error_size, "line %zu: node needs a kind", reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            while (cursor<rest_end && (*cursor==' ' || *cursor=='\t')) ++cursor;
            bool provided=cursor<rest_end;
            if (!graph_text(&cursor,rest_end,label,sizeof(label))) goto bad_graph_line;
            if (jfx_graph_add_node(graph, kind, provided ? label : NULL, &last) != JFX_SUCCESS) {
                set_error(out_error, out_error_size,
                    "line %zu: no node kind named '%s'", reader.line, kind);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            have_node = true;
        } else if (strcmp(key, "param") == 0) {
            /* `param NODE NAME VALUE` - the node is named rather than implied by
             * the last line, so a document can be edited without reordering. */
            uint32_t target = 0;
            char name[64];
            float value = 0.0f;
            if (!read_uint(&cursor, rest_end, &target) || !read_word(&cursor, rest_end, name,
                    sizeof(name)) || !read_float(&cursor, rest_end, &value)) {
                set_error(out_error, out_error_size,
                    "line %zu: param needs a node index, a name and a value", reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            if (target == 0u || target > jfx_graph_node_count(graph)) {
                set_error(out_error, out_error_size, "line %zu: param names no such node",
                    reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            const jfx_node_kind_t *kind = jfx_graph_node_kind(graph, target - 1u);
            size_t index = kind->param_count;
            for (size_t i = 0; i < kind->param_count; ++i) {
                if (strcmp(kind->params[i].name, name) == 0) {
                    index = i;
                    break;
                }
            }
            if (index == kind->param_count) {
                set_error(out_error, out_error_size, "line %zu: '%s' has no parameter '%s'",
                    reader.line, kind->name, name);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            if (jfx_graph_set_node_param(graph,target-1u,index,value)!=JFX_SUCCESS) goto bad_graph_line;
        } else if (!strcmp(key,"position")) {
            uint32_t target; float x,y;
            if (!read_uint(&cursor,rest_end,&target) || !target ||
                !read_float(&cursor,rest_end,&x) || !read_float(&cursor,rest_end,&y) ||
                jfx_graph_set_node_position(graph,target-1u,x,y)!=JFX_SUCCESS) goto bad_graph_line;
        } else if (strcmp(key, "string") == 0) {
            if (!have_node) {
                set_error(out_error, out_error_size, "line %zu: string before any node",
                    reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            uint32_t target = 0, slot = 0;
            if (!read_uint(&cursor, rest_end, &target) || !read_uint(&cursor, rest_end, &slot) ||
                target == 0u || target > jfx_graph_node_count(graph)) {
                set_error(out_error, out_error_size, "line %zu: string names no such node",
                    reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            /* Everything left on the line is the value, spaces included, because
             * a path may contain them. */
            while (cursor < rest_end && (*cursor == ' ' || *cursor == '\t')) {
                cursor++;
            }
            char copy[JFX_NODE_PATH_MAX]={0};
            if (!graph_text(&cursor,rest_end,copy,sizeof(copy))) goto bad_graph_line;
            const jfx_result_t set = jfx_graph_set_node_string(graph, target - 1u, slot, copy);
            if (set != JFX_SUCCESS) {
                set_error(out_error, out_error_size, "line %zu: that node has no slot %u",
                    reader.line, slot);
                jfx_graph_destroy(graph);
                return set;
            }
        } else if (strcmp(key, "output") == 0) {
            uint32_t index = 0;
            if (!read_uint(&cursor, rest_end, &index) ||
                index > jfx_graph_node_count(graph)) {
                set_error(out_error, out_error_size, "line %zu: output names no such node",
                    reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            output = index ? index - 1u : UINT32_MAX;
            saw_output = true;
        } else if (strcmp(key, "link") == 0) {
            /* Node numbers in a document are 1-based, matching the order the
             * `node` lines appear and what a person reading the file sees. */
            uint32_t from_index = 0, to_index = 0, from_port = 0, to_port = 0;
            char arrow[8];
            if (!read_uint(&cursor, rest_end, &from_index) ||
                !read_uint(&cursor, rest_end, &from_port) ||
                !read_word(&cursor, rest_end, arrow, sizeof(arrow)) ||
                !read_uint(&cursor, rest_end, &to_index) || !read_uint(&cursor, rest_end, &to_port) ||
                strcmp(arrow, "->") != 0) {
                set_error(out_error, out_error_size,
                    "line %zu: link needs FROM PORT -> TO PORT", reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            if (from_index == 0u || to_index == 0u || from_index > jfx_graph_node_count(graph) ||
                to_index > jfx_graph_node_count(graph)) {
                set_error(out_error, out_error_size, "line %zu: link names no such node",
                    reader.line);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            if (jfx_graph_connect(graph, from_index - 1u, from_port, to_index - 1u, to_port) !=
                JFX_SUCCESS) {
                set_error(out_error, out_error_size,
                    "line %zu: cannot link node %u port %u to node %u port %u", reader.line,
                    from_index, from_port, to_index, to_port);
                jfx_graph_destroy(graph);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        } else {
            set_error(out_error, out_error_size, "line %zu: unknown directive '%s'", reader.line,
                key);
            jfx_graph_destroy(graph);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        while (cursor<rest_end && (*cursor==' ' || *cursor=='\t')) ++cursor;
        if (cursor!=rest_end) goto bad_graph_line;
    }
    if (!have_node && !explicit_graph) {
        set_error(out_error, out_error_size, "the document declares no nodes");
        jfx_graph_destroy(graph);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!saw_output) output = last;
    if (!width || !height) {
        width = 1920u;
        height = 1080u;
    }
    *out_graph = graph;
    *out_output = output;
    *out_width = width;
    *out_height = height;
    return JFX_SUCCESS;
bad_graph_line:
    set_error(out_error,out_error_size,"line %zu: invalid graph arguments",reader.line);
    jfx_graph_destroy(graph); return JFX_ERROR_INVALID_ARGUMENT;
}

jfx_result_t jfx_project_save_graph(const jfx_graph_t *graph, uint32_t output, uint32_t width,
    uint32_t height, char *out_text, size_t out_size, size_t *out_written) {
    if (!graph || !out_text || !out_size || !width || !height ||
        (jfx_graph_node_count(graph) && output!=UINT32_MAX && output>=jfx_graph_node_count(graph))) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    int written = snprintf(out_text, out_size, "# JoltFX node graph\ngraph\nsize %u %u\n", width, height);
    if (written < 0 || (size_t)written >= out_size) {
        return JFX_ERROR_BACKEND_FAILURE;
    }
    size_t used = (size_t)written;
    for (size_t i = 0; i < jfx_graph_node_count(graph); ++i) {
        const jfx_node_kind_t *kind = jfx_graph_node_kind(graph, (uint32_t)i);
        const char *label = jfx_graph_node_label(graph, (uint32_t)i);
        float x=0,y=0; jfx_graph_node_position(graph,(uint32_t)i,&x,&y);
        if (!append(out_text,out_size,&used,"node %s ",kind->name) ||
            !append_token(out_text,out_size,&used,label?label:kind->label) ||
            !append(out_text,out_size,&used,"\nposition %zu %.9g %.9g\n",i+1,(double)x,(double)y)) return JFX_ERROR_BACKEND_FAILURE;
        const jfx_node_value_t *value = jfx_graph_node_value(graph, (uint32_t)i);
        for (size_t p = 0; p < kind->param_count; ++p) {
            written = snprintf(out_text + used, out_size - used, "param %zu %s %.9g\n", i + 1u,
                kind->params[p].name, (double)value->scalars[p]);
            if (written < 0 || used + (size_t)written >= out_size) {
                return JFX_ERROR_BACKEND_FAILURE;
            }
            used += (size_t)written;
        }
        for (size_t s = 0; s < kind->string_count; ++s) {
            const char *text_value = jfx_graph_node_string(graph, (uint32_t)i, s);
            if (text_value) {
                if (!append(out_text,out_size,&used,"string %zu %zu ",i+1,s) ||
                    !append_token(out_text,out_size,&used,text_value) ||
                    !append(out_text,out_size,&used,"\n")) return JFX_ERROR_BACKEND_FAILURE;
            }
        }
    }
    for (size_t i = 0; i < jfx_graph_node_count(graph); ++i) {
        const jfx_node_kind_t *kind = jfx_graph_node_kind(graph, (uint32_t)i);
        for (size_t p = 0; p < kind->input_count && p < JFX_GRAPH_MAX_INPUTS; ++p) {
            const int source = jfx_graph_input_source(graph, (uint32_t)i, p);
            if (source < 0) {
                continue;
            }
            const int source_port = jfx_graph_input_source_port(graph, (uint32_t)i, p);
            written = snprintf(out_text + used, out_size - used, "link %u %d -> %zu %zu\n",
                (unsigned)(source + 1), source_port, i + 1u, p);
            if (written < 0 || used + (size_t)written >= out_size) {
                return JFX_ERROR_BACKEND_FAILURE;
            }
            used += (size_t)written;
        }
        (void)kind;
    }
    {
        written = snprintf(out_text + used, out_size - used, "output %u\n", output==UINT32_MAX || !jfx_graph_node_count(graph)?0:output+1u);
        if (written < 0 || used + (size_t)written >= out_size) {
            return JFX_ERROR_BACKEND_FAILURE;
        }
        used += (size_t)written;
    }
    if (out_written) {
        *out_written = used;
    }
    return JFX_SUCCESS;
}

/* Appends to a bounded buffer, tracking the length. Every write in the writers
 * goes through this so a long name or path cannot run past the end. */
static bool append(char *out_text, size_t out_size, size_t *used, const char *format, ...) {
    if (*used >= out_size) {
        return false;
    }
    va_list args;
    va_start(args, format);
    const int n = vsnprintf(out_text + *used, out_size - *used, format, args);
    va_end(args);
    if (n < 0) {
        return false;
    }
    if (*used + (size_t)n >= out_size) {
        return false; /* the text did not fit */
    }
    *used += (size_t)n;
    return true;
}

static bool append_token(char *out,size_t cap,size_t *used,const char *text) {
    if (!append(out,cap,used,"\"")) return false;
    for (const char *p=text?text:"";*p;++p) {
        const char *escape=*p=='\n'?"\\n":*p=='\r'?"\\r":*p=='\t'?"\\t":*p=='\\'?"\\\\":*p=='\"'?"\\\"":NULL;
        if (escape?!append(out,cap,used,"%s",escape):!append(out,cap,used,"%c",*p)) return false;
    }
    return append(out,cap,used,"\"");
}

/* Renders the eight source parameters as a space-separated run the reader can
 * take straight back. Uses a rotating pair of buffers because the writer
 * interleaves this with other formatting. */
static const char *clip_params_text(const float *params, size_t count) {
    static char slots[2][256];
    static unsigned turn = 0u;
    char *buffer = slots[turn];
    turn = (turn + 1u) % 2u;
    size_t used = 0;
    for (size_t i = 0; i < count; ++i) {
        const int n = snprintf(buffer + used, sizeof(slots[0]) - used, " %.9g",
            (double)params[i]);
        if (n < 0 || used + (size_t)n >= sizeof(slots[0])) {
            break;
        }
        used += (size_t)n;
    }
    return buffer;
}

/* ---- Sequences ----------------------------------------------------------- */

jfx_result_t jfx_project_load_sequence(const char *text, size_t length, jfx_timeline_t **out_timeline,
    char *out_error, size_t out_error_size) {
    if (!text || !out_timeline) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!length) {
        length = strlen(text);
    }
    if (length > JFX_PROJECT_MAX_BYTES) {
        set_error(out_error, out_error_size, "document is %zu bytes, over the %u byte limit",
            length, (unsigned)JFX_PROJECT_MAX_BYTES);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    uint32_t width = 1920u, height = 1080u, fps_num = 30u, fps_den = 1u;
    jfx_timeline_t *timeline = jfx_timeline_create(width, height, fps_num, fps_den);
    if (!timeline) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
    uint32_t track = UINT32_MAX;
    uint32_t clip = UINT32_MAX;
    bool saw_fps=false;
    reader_t reader = { text, text + length, 0 };
    char line[4096];
    while (next_line(&reader, line, sizeof(line))) {
        const char *cursor = line;
        const char *const end = line + strlen(line);
        char key[64];
        if (!read_word(&cursor, end, key, sizeof(key))) {
            continue;
        }
        if (strcmp(key, "size") == 0) {
            if (!read_uint(&cursor, end, &width) || !read_uint(&cursor, end, &height) || !width ||
                !height) {
                goto bad_line;
            }
            jfx_timeline_destroy(timeline);
            timeline = jfx_timeline_create(width, height, fps_num, fps_den);
            if (!timeline) {
                return JFX_ERROR_OUT_OF_MEMORY;
            }
            track = UINT32_MAX;
        } else if (strcmp(key, "fps") == 0) {
            saw_fps=true;
            if (!read_uint(&cursor, end, &fps_num) || !read_uint(&cursor, end, &fps_den) ||
                !fps_num || !fps_den) {
                goto bad_line;
            }
            jfx_timeline_destroy(timeline);
            timeline = jfx_timeline_create(width, height, fps_num, fps_den);
            if (!timeline) {
                return JFX_ERROR_OUT_OF_MEMORY;
            }
            track = UINT32_MAX;
        } else if (strcmp(key, "track") == 0) {
            char name[128];
            read_word(&cursor, end, name, sizeof(name));
            track = jfx_timeline_add_track(timeline, name);
            if (track == UINT32_MAX) {
                set_error(out_error, out_error_size, "line %zu: too many tracks", reader.line);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_OUT_OF_MEMORY;
            }
            clip = UINT32_MAX;
        } else if (strcmp(key, "clip") == 0) {
            char source_name[64];
            if (track == UINT32_MAX) {
                set_error(out_error, out_error_size, "line %zu: clip before any track",
                    reader.line);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            if (!read_word(&cursor, end, source_name, sizeof(source_name))) {
                goto bad_line;
            }
            jfx_clip_desc_t desc;
            memset(&desc, 0, sizeof(desc));
            desc.source = jfx_clip_source_parse(source_name);
            if (desc.source == JFX_CLIP_SOURCE_COUNT) {
                set_error(out_error, out_error_size, "line %zu: unknown clip source '%s'",
                    reader.line, source_name);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            char path[512] = { 0 };
            if ((desc.source == JFX_CLIP_IMAGE || desc.source == JFX_CLIP_VIDEO || desc.source == JFX_CLIP_AUDIO)) {
                if (!read_word(&cursor, end, path, sizeof(path))) {
                    goto bad_line;
                }
                desc.image_path = path;
            }
            if (!read_frame(&cursor,end,&desc.start_frame) ||
                !read_frame(&cursor,end,&desc.length_frames) || !desc.length_frames) {
                goto bad_line;
            }
            for (size_t s = 0; s < sizeof(desc.source_params) / sizeof(desc.source_params[0]);
                 ++s) {
                if (!read_float(&cursor, end, &desc.source_params[s])) {
                    goto bad_line;
                }
            }
            char clip_name[JFX_TIMELINE_NAME_MAX] = { 0 };
            read_word(&cursor, end, clip_name, sizeof(clip_name));
            desc.name = clip_name;
            desc.opacity = 1.0f;
            desc.blend_mode = JFX_BLEND_NORMAL;
            desc.enabled = true;
            clip = jfx_timeline_add_clip(timeline, track, &desc);
            if (clip == UINT32_MAX) {
                set_error(out_error, out_error_size, "line %zu: cannot add that clip",
                    reader.line);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
        } else if (!strcmp(key,"track_state")) {
            uint32_t muted,solo,blend; float opacity;
            if (track==UINT32_MAX || !read_uint(&cursor,end,&muted) || !read_uint(&cursor,end,&solo) ||
                !read_float(&cursor,end,&opacity) || opacity<0 || opacity>1 ||
                !read_uint(&cursor,end,&blend) || blend>=JFX_BLEND_COUNT) goto bad_line;
            jfx_timeline_set_track_muted(timeline,track,muted!=0);
            jfx_timeline_set_track_solo(timeline,track,solo!=0);
            jfx_timeline_set_track_opacity(timeline,track,opacity);
            jfx_timeline_set_track_blend(timeline,track,(jfx_blend_mode_t)blend);
        } else if (!strcmp(key,"clip_state")) {
            uint64_t in; uint32_t enabled,blend; float opacity;
            if (clip==UINT32_MAX || !read_frame(&cursor,end,&in) ||
                !read_uint(&cursor,end,&enabled) || !read_float(&cursor,end,&opacity) || opacity<0 || opacity>1 ||
                !read_uint(&cursor,end,&blend) || blend>=JFX_BLEND_COUNT ||
                jfx_timeline_slip_clip(timeline,track,clip,(int64_t)in-(int64_t)jfx_timeline_clip_in_point(timeline,track,clip))!=JFX_SUCCESS) goto bad_line;
            jfx_timeline_set_clip_enabled(timeline,track,clip,enabled!=0);
            jfx_timeline_set_clip_opacity(timeline,track,clip,opacity);
            jfx_timeline_set_clip_blend(timeline,track,clip,(jfx_blend_mode_t)blend);
        } else if (!strcmp(key,"track_audio")) {
            float gain;
            if (!read_float(&cursor,end,&gain) || jfx_timeline_set_track_audio_gain(timeline,track,gain)!=JFX_SUCCESS) goto bad_line;
        } else if (!strcmp(key,"clip_audio")) {
            jfx_clip_audio_t audio={.size=sizeof(audio)}; uint32_t enabled;
            if (!read_uint(&cursor,end,&enabled) || enabled>1 || !read_float(&cursor,end,&audio.gain) ||
                !read_float(&cursor,end,&audio.pan) || !read_frame(&cursor,end,&audio.fade_in_frames) ||
                !read_frame(&cursor,end,&audio.fade_out_frames) || !read_frame(&cursor,end,&audio.reference_frames)) goto bad_line;
            audio.enabled=enabled!=0;
            if (jfx_timeline_set_clip_audio(timeline,track,clip,&audio)!=JFX_SUCCESS) goto bad_line;
        } else if (!strcmp(key,"clip_keys")) {
            int64_t offset;
            if (clip==UINT32_MAX || !read_signed_frame(&cursor,end,&offset) ||
                jfx_timeline_set_clip_key_offset(timeline,track,clip,offset)!=JFX_SUCCESS) goto bad_line;
        } else if (!strcmp(key,"effect_state")) {
            uint32_t enabled,blend,interp; float opacity;
            size_t count=jfx_timeline_effect_count(timeline,track,clip);
            if (!count || !read_uint(&cursor,end,&enabled) || !read_float(&cursor,end,&opacity) || opacity<0 || opacity>1 ||
                !read_uint(&cursor,end,&blend) || blend>=JFX_BLEND_COUNT ||
                !read_uint(&cursor,end,&interp) || interp>JFX_INTERP_SMOOTH) goto bad_line;
            uint32_t effect=(uint32_t)count-1;
            jfx_timeline_set_effect_enabled(timeline,track,clip,effect,enabled!=0);
            jfx_timeline_set_effect_opacity(timeline,track,clip,effect,opacity);
            jfx_timeline_set_effect_blend(timeline,track,clip,effect,(jfx_blend_mode_t)blend);
            jfx_timeline_set_effect_interp(timeline,track,clip,effect,(jfx_interp_t)interp);
        } else if (strcmp(key, "effect") == 0) {
            char kind[64];
            if (clip == UINT32_MAX) {
                set_error(out_error, out_error_size, "line %zu: effect before any clip",
                    reader.line);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            if (!read_word(&cursor, end, kind, sizeof(kind))) {
                goto bad_line;
            }
            const uint32_t effect = jfx_timeline_add_effect(timeline, track, clip, kind);
            if (effect == UINT32_MAX) {
                set_error(out_error, out_error_size,
                    "line %zu: '%s' is not a single-image effect", reader.line, kind);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            /* A kind's string fields take their text, then its parameters take
             * numbers, so `effect lut /path/to.cube 1.0` reads naturally. */
            const jfx_node_kind_t *descriptor =
                jfx_timeline_effect_kind_desc(timeline, track, clip, effect);
            for (size_t s = 0; s < descriptor->string_count; ++s) {
                char path[512];
                if (read_word(&cursor, end, path, sizeof(path))) {
                    jfx_timeline_set_effect_string(timeline, track, clip, effect, s, path);
                }
            }
            for (size_t p = 0; p < descriptor->param_count; ++p) {
                float value = 0.0f;
                if (!read_float(&cursor, end, &value)) {
                    break;
                }
                jfx_timeline_set_effect_param(timeline, track, clip, effect, p, value);
            }
        } else if (strcmp(key, "key") == 0 || strcmp(key, "disable") == 0 ||
            strcmp(key, "opacity") == 0) {
            if (clip == UINT32_MAX) {
                set_error(out_error, out_error_size, "line %zu: '%s' before any clip", reader.line,
                    key);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            uint32_t clip_index = 0, effect_index = 0;
            if (!read_uint(&cursor, end, &clip_index) || !read_uint(&cursor, end, &effect_index) ||
                clip_index == 0u || effect_index == 0u ||
                jfx_timeline_effect_count(timeline, track, clip_index - 1u) < effect_index) {
                set_error(out_error, out_error_size, "line %zu: '%s' names no such effect",
                    reader.line, key);
                jfx_timeline_destroy(timeline);
                return JFX_ERROR_INVALID_ARGUMENT;
            }
            clip_index--;
            effect_index--;
            if (strcmp(key, "disable") == 0) {
                jfx_timeline_set_effect_enabled(timeline, track, clip_index, effect_index, false);
            } else if (strcmp(key, "opacity") == 0) {
                float amount = 1.0f;
                if (!read_float(&cursor, end, &amount)) {
                    goto bad_line;
                }
                jfx_timeline_set_effect_opacity(timeline, track, clip_index, effect_index, amount);
            } else {
                uint32_t param = 0;
                uint64_t frame_value = 0; float amount = 0.0f;
                if (!read_uint(&cursor, end, &param) || !read_frame(&cursor, end, &frame_value) ||
                    !read_float(&cursor, end, &amount)) {
                    goto bad_line;
                }
                if (jfx_timeline_add_key(timeline,track,clip_index,effect_index,(size_t)param,frame_value,amount)!=JFX_SUCCESS) goto bad_line;
            }
        } else {
            set_error(out_error, out_error_size, "line %zu: unknown directive '%s'", reader.line,
                key);
            jfx_timeline_destroy(timeline);
            return JFX_ERROR_INVALID_ARGUMENT;
        }
        continue;
    bad_line:
        set_error(out_error, out_error_size, "line %zu: '%s' has the wrong arguments", reader.line,
            key);
        jfx_timeline_destroy(timeline);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (jfx_timeline_track_count(timeline) == 0u && !saw_fps) {
        set_error(out_error, out_error_size, "the document declares no tracks");
        jfx_timeline_destroy(timeline);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    *out_timeline = timeline;
    return JFX_SUCCESS;
}

jfx_result_t jfx_project_save_sequence(const jfx_timeline_t *timeline, char *out_text,
    size_t out_size, size_t *out_written) {
    if (!timeline || !out_text || !out_size) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    int written = snprintf(out_text, out_size, "# JoltFX sequence\nsize %u %u\nfps %u %u\n",
        jfx_timeline_width(timeline), jfx_timeline_height(timeline),
        jfx_timeline_fps_num(timeline), jfx_timeline_fps_den(timeline));
    if (written < 0 || (size_t)written >= out_size) {
        return JFX_ERROR_BACKEND_FAILURE;
    }
    size_t used = (size_t)written;
    for (size_t t = 0; t < jfx_timeline_track_count(timeline); ++t) {
        const uint32_t track = (uint32_t)t;
        if (!append(out_text,out_size,&used,"track ") ||
            !append_token(out_text,out_size,&used,jfx_timeline_track_name(timeline,track)) ||
            !append(out_text,out_size,&used,"\ntrack_state %u %u %.9g %u\n",
                jfx_timeline_track_muted(timeline,track),jfx_timeline_track_solo(timeline,track),
                (double)jfx_timeline_track_opacity(timeline,track),(unsigned)jfx_timeline_track_blend(timeline,track))) return JFX_ERROR_BACKEND_FAILURE;
        if (!append(out_text,out_size,&used,"track_audio %.9g\n",(double)jfx_timeline_track_audio_gain(timeline,track))) return JFX_ERROR_BACKEND_FAILURE;

        for (size_t c = 0; c < jfx_timeline_clip_count(timeline, track); ++c) {
            const uint32_t clip = (uint32_t)c;
            const jfx_clip_source_t source = jfx_timeline_clip_source(timeline, track, clip);
            const float *params = jfx_timeline_clip_params(timeline, track, clip);
            const char *path = jfx_timeline_clip_path(timeline, track, clip);
            const char *name = jfx_timeline_clip_name(timeline, track, clip);
            if (!append(out_text,out_size,&used,"clip %s ",jfx_clip_source_name(source))) return JFX_ERROR_BACKEND_FAILURE;
            if ((source==JFX_CLIP_IMAGE || source==JFX_CLIP_VIDEO || source==JFX_CLIP_AUDIO) &&
                (!append_token(out_text,out_size,&used,path) || !append(out_text,out_size,&used," "))) return JFX_ERROR_BACKEND_FAILURE;
            if (!append(out_text, out_size, &used, "%llu %llu%s ",
                    (unsigned long long)jfx_timeline_clip_start(timeline, track, clip),
                    (unsigned long long)jfx_timeline_clip_length(timeline, track, clip),
                    clip_params_text(params, 8)) || !append_token(out_text,out_size,&used,name) ||
                !append(out_text,out_size,&used,"\nclip_state %llu %u %.9g %u\n",
                    (unsigned long long)jfx_timeline_clip_in_point(timeline,track,clip),
                    jfx_timeline_clip_enabled(timeline,track,clip),(double)jfx_timeline_clip_opacity(timeline,track,clip),
                    (unsigned)jfx_timeline_clip_blend(timeline,track,clip))) {
                return JFX_ERROR_BACKEND_FAILURE;
            }

            if (!append(out_text,out_size,&used,"clip_keys %lld\n",(long long)jfx_timeline_clip_key_offset(timeline,track,clip))) return JFX_ERROR_BACKEND_FAILURE;
            jfx_clip_audio_t audio={.size=sizeof(audio)};
            if (jfx_timeline_get_clip_audio(timeline,track,clip,&audio)!=JFX_SUCCESS ||
                !append(out_text,out_size,&used,"clip_audio %u %.9g %.9g %llu %llu %llu\n",audio.enabled,
                    (double)audio.gain,(double)audio.pan,(unsigned long long)audio.fade_in_frames,
                    (unsigned long long)audio.fade_out_frames,(unsigned long long)audio.reference_frames)) return JFX_ERROR_BACKEND_FAILURE;

            const size_t effect_count = jfx_timeline_effect_count(timeline, track, clip);
            for (size_t e = 0; e < effect_count; ++e) {
                const uint32_t effect = (uint32_t)e;
                const jfx_node_kind_t *kind =
                    jfx_timeline_effect_kind_desc(timeline, track, clip, effect);
                if (!kind) {
                    continue;
                }
                if (!append(out_text, out_size, &used, "effect %s", kind->name)) {
                    return JFX_ERROR_BACKEND_FAILURE;
                }
                for (size_t s = 0; s < kind->string_count; ++s) {
                    const char *value = jfx_timeline_effect_string(timeline, track, clip, effect, s);
                    if (!append(out_text, out_size, &used, " \"")) return JFX_ERROR_BACKEND_FAILURE;
                    for (const char *p = value ? value : ""; *p; ++p) {
                        const char *escape = *p == '\n' ? "\\n" : *p == '\r' ? "\\r" :
                            *p == '\t' ? "\\t" : *p == '\\' ? "\\\\" : *p == '"' ? "\\\"" : NULL;
                        if (escape ? !append(out_text, out_size, &used, "%s", escape) :
                            !append(out_text, out_size, &used, "%c", *p)) return JFX_ERROR_BACKEND_FAILURE;
                    }
                    if (!append(out_text, out_size, &used, "\"")) return JFX_ERROR_BACKEND_FAILURE;
                }
                for (size_t pi = 0; pi < kind->param_count; ++pi) {
                    if (!append(out_text, out_size, &used, " %.9g",
                            (double)jfx_timeline_effect_param(timeline, track, clip, effect, pi))) {
                        return JFX_ERROR_BACKEND_FAILURE;
                    }
                }
                /* Trailing state, written as separate directives so the reader
                 * stays positional. */
                if (used + 1 >= out_size || out_text[used] != '\0') {
                    return JFX_ERROR_BACKEND_FAILURE;
                }
                out_text[used++] = '\n';
                out_text[used] = '\0';
                if (!append(out_text,out_size,&used,"effect_state %u %.9g %u %u\n",
                    jfx_timeline_effect_enabled(timeline,track,clip,effect),
                    (double)jfx_timeline_effect_opacity(timeline,track,clip,effect),
                    (unsigned)jfx_timeline_effect_blend(timeline,track,clip,effect),
                    (unsigned)jfx_timeline_effect_interp(timeline,track,clip,effect))) return JFX_ERROR_BACKEND_FAILURE;
                for (size_t pi = 0; pi < kind->param_count; ++pi) {
                    const size_t keys =
                        jfx_timeline_key_count(timeline, track, clip, effect, pi);
                    for (size_t k = 0; k < keys; ++k) {
                        uint64_t frame = 0;
                        float value = 0.0f;
                        if (!jfx_timeline_key_by_index(timeline, track, clip, effect, pi, k, &frame,
                                &value)) {
                            break;
                        }
                        if (!append(out_text, out_size, &used, "key %zu %u %zu %llu %.9g\n",
                                c + 1u, effect + 1u, pi, (unsigned long long)frame,
                                (double)value)) {
                            return JFX_ERROR_BACKEND_FAILURE;
                        }
                    }
                }
                if (!jfx_timeline_effect_enabled(timeline, track, clip, effect) &&
                    !append(out_text, out_size, &used, "disable %zu %u\n", c + 1u, effect + 1u)) {
                    return JFX_ERROR_BACKEND_FAILURE;
                }
                const float effect_opacity =
                    jfx_timeline_effect_opacity(timeline, track, clip, effect);
                if (effect_opacity < 1.0f &&
                    !append(out_text, out_size, &used, "opacity %zu %u %.9g\n", c + 1u,
                        effect + 1u, (double)effect_opacity)) {
                    return JFX_ERROR_BACKEND_FAILURE;
                }
            }
        }
    }
    if (out_written) {
        *out_written = used;
    }
    return JFX_SUCCESS;
}

jfx_result_t jfx_project_kind_of(const char *text, size_t length, jfx_project_kind_t *out_kind,
    char *out_error, size_t out_error_size) {
    if (!text || !out_kind) {
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    if (!length) {
        length = strlen(text);
    }
    reader_t reader = { text, text + length, 0 };
    char line[512];
    bool saw_size = false;
    while (next_line(&reader, line, sizeof(line))) {
        const char *cursor = line;
        const char *const end = line + strlen(line);
        char key[64];
        if (!read_word(&cursor, end, key, sizeof(key))) {
            continue;
        }
        if (strcmp(key, "size") == 0) {
            saw_size = true;
            continue;
        }
        if (strcmp(key, "track") == 0) {
            *out_kind = JFX_PROJECT_KIND_SEQUENCE;
            return JFX_SUCCESS;
        }
        if (strcmp(key, "node") == 0 || !strcmp(key,"graph")) {
            *out_kind = JFX_PROJECT_KIND_GRAPH;
            return JFX_SUCCESS;
        }
        if (strcmp(key, "fps") == 0) {
            *out_kind=JFX_PROJECT_KIND_SEQUENCE;
            return JFX_SUCCESS;
        }
        if (strcmp(key, "link") == 0 || strcmp(key, "param") == 0 || strcmp(key, "string") == 0 ||
            strcmp(key, "output") == 0) {
            *out_kind = JFX_PROJECT_KIND_GRAPH;
            return JFX_SUCCESS;
        }
        set_error(out_error, out_error_size, "line %zu: unknown directive '%s'", reader.line, key);
        return JFX_ERROR_INVALID_ARGUMENT;
    }
    (void)saw_size;
    set_error(out_error, out_error_size, "the document is empty");
    return JFX_ERROR_INVALID_ARGUMENT;
}
