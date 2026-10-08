/* Colour grading, node compositing and non-linear editing from the command line.
 *
 * These commands drive the same model the GUIs do: a graph document is a grade,
 * a sequence document is a timeline, and both are read and written by the
 * interchange format in core. So anything a panel can do is scriptable, and a
 * render is reproducible from a text file alone. */

#include "tilly/memory.h"
#include "commands.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "jfx/jfx_compose.h"
#include "jfx/jfx_color.h"
#include "tilly/containers.h"
#include "jfx/jfx_lut.h"
#include "jfx/jfx_project.h"
#include "jfx/jfx_timeline.h"
#include "jfx/jfx_editor.h"

#define MAX_PATH 1024

static int fail(const char *format, ...) {
    va_list args;
    va_start(args, format);
    fputs("error: ", stderr);
    vfprintf(stderr, format, args);
    fputc('\n', stderr);
    va_end(args);
    return 1;
}

static char *read_file(const char *path, size_t *out_size) {
    FILE *file = fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    if (length < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);
    char *text = tilly_mem_alloc((size_t)length + 1u);
    if (!text) {
        fclose(file);
        return NULL;
    }
    const size_t got = fread(text, 1, (size_t)length, file);
    fclose(file);
    text[got] = '\0';
    if (out_size) {
        *out_size = got;
    }
    return text;
}

static int write_ppm(const char *path, const uint8_t *rgba, uint32_t width, uint32_t height) {
    FILE *file = fopen(path, "wb");
    if (!file) {
        return 1;
    }
    fprintf(file, "P6\n%u %u\n255\n", width, height);
    for (size_t i = 0; i < (size_t)width * height; ++i) {
        const uint8_t rgb[3] = { rgba[i * 4u], rgba[i * 4u + 1u], rgba[i * 4u + 2u] };
        if (fwrite(rgb, 1, sizeof(rgb), file) != sizeof(rgb)) {
            fclose(file);
            return 1;
        }
    }
    return fclose(file) == 0 ? 0 : 1;
}

/* ---- nodes --------------------------------------------------------------- */

int cmd_color(int argc,char **argv,int calibration) {
    jfx_color_section_t section=calibration?JFX_COLOR_CALIBRATION:JFX_COLOR_GRADING;
    if (argc==1 && !strcmp(argv[0],"list")) {
        puts(calibration?"Color Calibration":"Color Grading");
        for (size_t i=0;i<jfx_color_kind_count();++i) {
            const jfx_node_kind_t *k=jfx_color_kind_at(i);
            if (jfx_color_section(k)!=section) continue;
            printf("%s: %s\n",k->name,k->label);
            for (size_t p=0;p<k->param_count;++p) printf("  %s=%g [%g, %g]%s\n",k->params[p].name,
                (double)k->params[p].default_value,(double)k->params[p].minimum,(double)k->params[p].maximum,k->params[p].integral?" integer":"");
            if (k->string_count) puts("  --lut FILE (1D/3D LUT or OpenColorIO-supported transform)");
        }
        return 0;
    }
    if (argc<4 || strcmp(argv[0],"apply")) { print_command_help(calibration?"calibration":"grade"); return 1; }
    const jfx_node_kind_t *k=jfx_node_kind_find(argv[1]);
    if (!k || jfx_color_section(k)!=section) return fail("operator does not belong to this color section");
    jfx_graph_t *g=jfx_graph_create(); uint32_t source=0, grade=0;
    jfx_image_t image={.size=sizeof(image)}; uint8_t *pixels=NULL; int result=1;
    if (!g || jfx_image_load(argv[2],NULL,&image)!=JFX_SUCCESS) { fail("cannot load input image"); goto done; }
    if (jfx_graph_add_node(g,"image",NULL,&source)!=JFX_SUCCESS ||
        jfx_graph_set_node_string(g,source,0,argv[2])!=JFX_SUCCESS ||
        jfx_graph_add_node(g,k->name,NULL,&grade)!=JFX_SUCCESS ||
        jfx_graph_connect(g,source,0,grade,0)!=JFX_SUCCESS) goto done;
    for (int i=4;i<argc;++i) {
        if (!strcmp(argv[i],"--lut")) {
            if (++i>=argc || !k->string_count || jfx_graph_set_node_string(g,grade,0,argv[i])!=JFX_SUCCESS) { fail("invalid LUT option"); goto done; }
            continue;
        }
        const char *equals=strchr(argv[i],'='); size_t p=0;
        if (!equals) { fail("expected name=value"); goto done; }
        for (;p<k->param_count;++p) if (strlen(k->params[p].name)==(size_t)(equals-argv[i]) && !strncmp(k->params[p].name,argv[i],(size_t)(equals-argv[i]))) break;
        char *end=NULL; double value=strtod(equals+1,&end);
        if (p==k->param_count || end==equals+1 || *end || !isfinite(value) || value<(double)k->params[p].minimum ||
            value>(double)k->params[p].maximum || (k->params[p].integral && floor(value)!=value)) { fail("invalid parameter '%s'",argv[i]); goto done; }
        jfx_graph_node_value_mut(g,grade)->scalars[p]=(float)value;
    }
    if (image.width>4096 || image.height>4096) { fail("color image dimensions exceed 4096"); goto done; }
    pixels=tilly_container_alloc(image.width*image.height*4);
    if (!pixels) goto done;
    jfx_result_t r=jfx_graph_render(g,grade,(uint32_t)image.width,(uint32_t)image.height,0,pixels);
    if (r!=JFX_SUCCESS) { fail("color render failed: %s",jfx_result_to_string(r)); goto done; }
    result=write_ppm(argv[3],pixels,(uint32_t)image.width,(uint32_t)image.height);
done:
    tilly_container_free(pixels); jfx_image_release(&image); jfx_graph_destroy(g); return result;
}

int cmd_nodes(int argc, char **argv) {
    const char *only = NULL;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("usage: joltfx nodes [NAME]\n\nLists the compositing node library, or describes\n"
                   "one node: its ports and its parameters.\n");
            return 0;
        }
        only = argv[i];
    }
    if (!only) {
        printf("%zu node kinds\n\n", jfx_node_kind_count());
        const char *category = NULL;
        for (size_t i = 0; i < jfx_node_kind_count(); ++i) {
            const jfx_node_kind_t *kind = jfx_node_kind_at(i);
            if (!category || strcmp(category, kind->category) != 0) {
                category = kind->category;
                printf("\n%s:\n", category);
            }
            printf("  %-16s %s\n", kind->name, kind->label);
        }
        printf("\n`joltfx nodes NAME` describes one node's ports and parameters.\n");
        return 0;
    }
    const jfx_node_kind_t *kind = jfx_node_kind_find(only);
    if (!kind) {
        return fail("no node kind named '%s'; try `joltfx nodes`", only);
    }
    printf("%s  (%s)\n", kind->label, kind->name);
    if (kind->input_count == 0u) {
        printf("  inputs:   none, it is a source\n");
    } else {
        printf("  inputs:\n");
        for (size_t i = 0; i < kind->input_count; ++i) {
            const jfx_port_desc_t *port = &kind->inputs[i];
            printf("    %-8s %-6s %-8s %s\n", port->name, jfx_port_type_name(port->type),
                port->required ? "required" : "optional", port->label);
        }
    }
    if (kind->output_count > 0u) {
        printf("  outputs:\n");
        for (size_t i = 0; i < kind->output_count; ++i) {
            printf("    %-8s %s\n", kind->outputs[i].name,
                jfx_port_type_name(kind->outputs[i].type));
        }
    }
    if (kind->param_count > 0u) {
        printf("  parameters:\n");
        for (size_t i = 0; i < kind->param_count; ++i) {
            const jfx_param_desc_t *param = &kind->params[i];
            printf("    %-12s %-14s [%g, %g] default %g\n", param->name, param->label,
                (double)param->minimum, (double)param->maximum, (double)param->default_value);
        }
    }
    if (kind->string_count > 0u) {
        printf("  fields:\n");
        for (size_t i = 0; i < kind->string_count; ++i) {
            printf("    %s\n", kind->strings[i]);
        }
    }
    if (kind->input_count == 1u && kind->inputs[0].type == JFX_PORT_IMAGE) {
        printf("\n  usable as a clip effect in a sequence document.\n");
    }
    return 0;
}

/* ---- lut ----------------------------------------------------------------- */

int cmd_lut(int argc, char **argv) {
    if (argc < 1) {
        printf("usage: joltfx lut info FILE\n"
               "       joltfx lut convert IN OUT [--as cube|3dl|spi]\n"
               "       joltfx lut apply LUT IMAGE OUT --width W --height H --mix M\n");
        return 1;
    }
    const char *sub = argv[0];
    if (strcmp(sub, "info") == 0) {
        if (argc < 2) {
            return fail("lut info needs a file");
        }
        jfx_lut_t *lut = NULL;
        char error[256] = { 0 };
        const jfx_result_t status = jfx_lut_load_auto(argv[1], &lut, error, sizeof(error));
        if (status != JFX_SUCCESS) {
            return fail("%s", error);
        }
        printf("file:      %s\n", argv[1]);
        printf("format:    %s\n", jfx_lut_format_name(lut));
        printf("shape:     %s %zux%zux%zu\n", jfx_lut_shape_name(lut->shape), lut->width,
            lut->height, lut->depth);
        printf("entries:   %zu\n", lut->entry_count);
        printf("channels:  %u\n", lut->channels);
        printf("domain:    [%g, %g] per channel\n", (double)lut->domain_min[0],
            (double)lut->domain_max[0]);
        if (lut->title[0]) {
            printf("title:     %s\n", lut->title);
        }
        if (lut->description[0]) {
            printf("detail:    %s\n", lut->description);
        }
        jfx_lut_destroy(lut);
        return 0;
    }
    if (strcmp(sub, "convert") == 0) {
        if (argc < 3) {
            return fail("lut convert needs an input and an output path");
        }
        jfx_lut_t *lut = NULL;
        char error[256] = { 0 };
        if (jfx_lut_load_auto(argv[1], &lut, error, sizeof(error)) != JFX_SUCCESS) {
            return fail("%s", error);
        }
        const char *output = argv[2];
        jfx_result_t status = JFX_ERROR_INVALID_ARGUMENT;
        /* The format follows the output extension, so no flag is needed for the
         * usual case; --as overrides it. */
        const char *forced = NULL;
        for (int i = 3; i < argc; ++i) {
            if (strcmp(argv[i], "--as") == 0 && i + 1 < argc) {
                forced = argv[++i];
            }
        }
        const char *dot = strrchr(output, '.');
        const char *kind = forced ? forced : (dot ? dot + 1 : "");
        const char *written = "Adobe .cube";
        if (strcmp(kind, "3dl") == 0) {
            status = jfx_lut_write_3dl(lut, output, error, sizeof(error));
            written = "Autodesk .3dl";
        } else if (strcmp(kind, "spi1d") == 0 || strcmp(kind, "spi3d") == 0) {
            status = jfx_lut_write_spi(lut, output, error, sizeof(error));
            written = "Sony .spi";
        } else {
            status = jfx_lut_write_cube(lut, output, error, sizeof(error));
        }
        if (status != JFX_SUCCESS) {
            jfx_lut_destroy(lut);
            return fail("%s", error[0] ? error : "cannot write that format");
        }
        /* Report the format written, not the one read: that is what the caller
         * asked for and what the file now holds. */
        printf("wrote %s (%s)\n", output, written);
        jfx_lut_destroy(lut);
        return 0;
    }
    if (strcmp(sub, "apply") == 0) {
        if (argc < 4) {
            return fail("lut apply needs a LUT, an image and an output path");
        }
        jfx_lut_t *lut = NULL;
        char error[256] = { 0 };
        if (jfx_lut_load_auto(argv[1], &lut, error, sizeof(error)) != JFX_SUCCESS) {
            return fail("%s", error);
        }
        float mix = 1.0f;
        for (int i = 4; i + 1 < argc; ++i) {
            if (strcmp(argv[i], "--mix") == 0) {
                mix = (float)atof(argv[++i]);
            }
        }
        jfx_image_t image = { .size = sizeof(image) };
        if (jfx_image_load(argv[2], NULL, &image) != JFX_SUCCESS) {
            jfx_lut_destroy(lut);
            return fail("cannot decode '%s'", argv[2]);
        }
        const uint32_t width = image.width, height = image.height;
        const size_t count = (size_t)width * height;
        uint8_t *out = tilly_mem_alloc(count * 4u);
        if (!out) {
            jfx_image_release(&image);
            jfx_lut_destroy(lut);
            return fail("out of memory");
        }
        const jfx_result_t status = jfx_lut_apply_rgba8(lut, image.pixels, count, mix, out);
        jfx_image_release(&image);
        jfx_lut_destroy(lut);
        if (status != JFX_SUCCESS) {
            tilly_mem_free(out);
            return fail("cannot apply that LUT");
        }
        const int written = write_ppm(argv[3], out, width, height);
        tilly_mem_free(out);
        if (written != 0) {
            return fail("cannot write '%s'", argv[3]);
        }
        printf("wrote %s\n", argv[3]);
        return 0;
    }
    return fail("unknown lut subcommand '%s'", sub);
}

/* ---- render-graph -------------------------------------------------------- */

static int render_graph_document(const char *path, const char *output, uint32_t width,
    uint32_t height, float time_seconds) {
    size_t length = 0;
    char *text = read_file(path, &length);
    if (!text) {
        return fail("cannot read '%s'", path);
    }
    jfx_editor_t *editor=jfx_editor_create(320,180);
    if (!editor) { tilly_mem_free(text); return fail("out of memory"); }
    char error[256] = { 0 };
    const jfx_result_t status=jfx_editor_load(editor,text,length,error,sizeof(error));
    tilly_mem_free(text);
    if (status != JFX_SUCCESS || jfx_editor_kind(editor)!=JFX_PROJECT_KIND_GRAPH) {
        jfx_editor_destroy(editor);
        return fail("%s", error);
    }
    if (!width) {
        width = jfx_editor_graph_width(editor);
    }
    if (!height) {
        height = jfx_editor_graph_height(editor);
    }
    jfx_result_t rendered=jfx_editor_write_graph(editor,UINT32_MAX,time_seconds,width,height,output);
    jfx_editor_destroy(editor);
    return rendered==JFX_SUCCESS?0:fail("cannot render/export that graph: %s",jfx_result_to_string(rendered));
}

int cmd_render_graph(int argc, char **argv) {
    const char *document = NULL;
    const char *output = NULL;
    uint32_t width = 0, height = 0;
    float time_seconds = 0.0f;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--time") == 0 && i + 1 < argc) {
            time_seconds = (float)atof(argv[++i]);
        } else if (!document) {
            document = argv[i];
        }
    }
    if (!document || !output) {
        printf("usage: joltfx render-graph DOC.jfx -o OUT.ppm [--width W] [--height H] "
               "[--time S]\n\n"
               "Renders a grade: a graph document of node and link lines.\n");
        return 1;
    }
    return render_graph_document(document, output, width, height, time_seconds);
}

/* ---- render-sequence ----------------------------------------------------- */

int cmd_render_sequence(int argc, char **argv) {
    const char *document = NULL;
    const char *prefix = NULL;
    uint32_t width = 0, height = 0;
    long start = 0;
    long end = -1;
    for (int i = 0; i < argc; ++i) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            prefix = argv[++i];
        } else if (strcmp(argv[i], "--width") == 0 && i + 1 < argc) {
            width = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--height") == 0 && i + 1 < argc) {
            height = (uint32_t)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--start") == 0 && i + 1 < argc) {
            start = atol(argv[++i]);
        } else if (strcmp(argv[i], "--end") == 0 && i + 1 < argc) {
            end = atol(argv[++i]);
        } else if (!document) {
            document = argv[i];
        }
    }
    if (!document || !prefix) {
        printf("usage: joltfx render-sequence DOC.jfx -o PREFIX [--start N] [--end N] "
               "[--width W] [--height H]\n\n"
               "Renders a non-linear edit: a sequence document of track, clip and\n"
               "effect lines. Writes PREFIX0000.ppm, PREFIX0001.ppm and so on.\n");
        return 1;
    }
    size_t length = 0;
    char *text = read_file(document, &length);
    if (!text) {
        return fail("cannot read '%s'", document);
    }
    jfx_editor_t *editor=jfx_editor_create(320,180);
    if (!editor) { tilly_mem_free(text); return fail("out of memory"); }
    char error[256] = { 0 };
    const jfx_result_t status =
        jfx_editor_load(editor,text,length,error,sizeof(error));
    tilly_mem_free(text);
    if (status != JFX_SUCCESS || jfx_editor_kind(editor)!=JFX_PROJECT_KIND_SEQUENCE) {
        jfx_editor_destroy(editor);
        return fail("%s", error);
    }
    jfx_timeline_t *timeline=jfx_editor_timeline(editor);
    if (!width) {
        width = jfx_timeline_width(timeline);
    }
    if (!height) {
        height = jfx_timeline_height(timeline);
    }
    const uint64_t duration = jfx_timeline_duration(timeline);
    if (end < 0) {
        end = (long)duration;
    }
    if (end == 0) {
        end = 1; /* an empty sequence still writes one frame rather than none */
    }
    if (!width || !height || width>4096 || height>4096 || start<0 || end<start) {
        jfx_editor_destroy(editor); return fail("invalid raster or frame range");
    }
    uint8_t *pixels = tilly_container_alloc((size_t)width * height * 4u);
    if (!pixels) {
        jfx_editor_destroy(editor);
        return fail("out of memory");
    }
    for (long frame = start; frame < end; ++frame) {
        if (jfx_editor_render_frame(editor,(uint64_t)frame,width,height,pixels,(size_t)width*height*4) != JFX_SUCCESS) {
            tilly_container_free(pixels);
            jfx_editor_destroy(editor);
            return fail("cannot render frame %ld", frame);
        }
        char path[MAX_PATH];
        snprintf(path, sizeof(path), "%s%04ld.ppm", prefix, frame);
        if (write_ppm(path, pixels, width, height) != 0) {
            tilly_container_free(pixels);
            jfx_editor_destroy(editor);
            return fail("cannot write '%s'", path);
        }
    }
    tilly_container_free(pixels);
    printf("wrote %ld frame%s from %s (%ux%u, %g fps)\n", end - start,
        (end - start) == 1 ? "" : "s", document, width, height, jfx_timeline_fps(timeline));
    jfx_editor_destroy(editor);
    return 0;
}

/* ---- project ------------------------------------------------------------- */

int cmd_project(int argc, char **argv) {
    if (argc < 2) {
        printf("usage: joltfx project info DOC.jfx\n"
               "       joltfx project render DOC.jfx -o OUT.ppm\n");
        return 1;
    }
    const char *sub = argv[0];
    const char *document = argv[1];
    if (strcmp(sub, "info") == 0) {
        size_t length = 0;
        char *text = read_file(document, &length);
        if (!text) {
            return fail("cannot read '%s'", document);
        }
        jfx_project_kind_t kind = JFX_PROJECT_KIND_SEQUENCE;
        char error[256] = { 0 };
        const jfx_result_t detected =
            jfx_project_kind_of(text, length, &kind, error, sizeof(error));
        tilly_mem_free(text);
        if (detected != JFX_SUCCESS) {
            return fail("%s", error);
        }
        printf("kind: %s\n", kind == JFX_PROJECT_KIND_GRAPH ? "graph (a grade)" : kind==JFX_PROJECT_KIND_SCENE3D?"scene3d":"sequence");
        if (kind==JFX_PROJECT_KIND_SCENE3D) {
            text=read_file(document,&length); if (!text) return fail("cannot read '%s'",document);
            jfx_scene3d_t *scene=jfx_scene3d_create();
            jfx_result_t status=scene?jfx_scene3d_load(scene,text,length,error,sizeof(error)):JFX_ERROR_OUT_OF_MEMORY;
            tilly_mem_free(text);
            if (status!=JFX_SUCCESS) { jfx_scene3d_destroy(scene); return fail("%s",error); }
            printf("rate:       %u fps\nduration:   %u frames\nobjects:    %u\n",jfx_scene3d_fps(scene),jfx_scene3d_frames(scene),jfx_scene3d_object_count(scene));
            jfx_scene3d_destroy(scene); return 0;
        }
        if (kind == JFX_PROJECT_KIND_GRAPH) {
            jfx_graph_t *graph = NULL;
            uint32_t node = 0, width = 0, height = 0;
            text = read_file(document, &length);
            if (!text) {
                return fail("cannot read '%s'", document);
            }
            const jfx_result_t status = jfx_project_load_graph(text, length, &graph, &node, &width,
                &height, error, sizeof(error));
            tilly_mem_free(text);
            if (status != JFX_SUCCESS) {
                return fail("%s", error);
            }
            printf("raster:  %ux%u\n", width, height);
            printf("output:  %s\n", node==UINT32_MAX?"none":jfx_graph_node_label(graph, node));
            char summary[1024];
            size_t written = 0;
            if (jfx_graph_describe(graph, summary, sizeof(summary), &written) == JFX_SUCCESS) {
                printf("%s\n", summary);
            }
            jfx_graph_destroy(graph);
        } else {
            jfx_timeline_t *timeline = NULL;
            text = read_file(document, &length);
            if (!text) {
                return fail("cannot read '%s'", document);
            }
            const jfx_result_t status =
                jfx_project_load_sequence(text, length, &timeline, error, sizeof(error));
            tilly_mem_free(text);
            if (status != JFX_SUCCESS) {
                return fail("%s", error);
            }
            printf("raster:     %ux%u\n", jfx_timeline_width(timeline),
                jfx_timeline_height(timeline));
            printf("rate:       %u/%u (%g fps)\n", jfx_timeline_fps_num(timeline),
                jfx_timeline_fps_den(timeline), jfx_timeline_fps(timeline));
            printf("duration:   %llu frames", (unsigned long long)jfx_timeline_duration(timeline));
            char timecode[32];
            if (jfx_timeline_timecode(timeline, jfx_timeline_duration(timeline), timecode,
                    sizeof(timecode)) == JFX_SUCCESS) {
                printf(" (%s)", timecode);
            }
            printf("\n");
            for (size_t t = 0; t < jfx_timeline_track_count(timeline); ++t) {
                char summary[128];
                if (jfx_timeline_describe_track(timeline, (uint32_t)t, summary, sizeof(summary)) ==
                    JFX_SUCCESS) {
                    printf("  %-12s %s\n", jfx_timeline_track_name(timeline, (uint32_t)t),
                        summary);
                }
                for (size_t c = 0; c < jfx_timeline_clip_count(timeline, (uint32_t)t); ++c) {
                    printf("    clip %s [%llu..%llu) %zu effect%s\n",
                        jfx_timeline_clip_name(timeline, (uint32_t)t, (uint32_t)c),
                        (unsigned long long)jfx_timeline_clip_start(timeline, (uint32_t)t, (uint32_t)c),
                        (unsigned long long)(jfx_timeline_clip_start(timeline, (uint32_t)t,
                                               (uint32_t)c) +
                            jfx_timeline_clip_length(timeline, (uint32_t)t, (uint32_t)c)),
                        jfx_timeline_effect_count(timeline, (uint32_t)t, (uint32_t)c),
                        jfx_timeline_effect_count(timeline, (uint32_t)t, (uint32_t)c) == 1u ? ""
                                                                                            : "s");
                }
            }
            jfx_timeline_destroy(timeline);
        }
        return 0;
    }
    if (strcmp(sub, "render") == 0) {
        const char *output = NULL;
        for (int i = 2; i + 1 < argc; ++i) {
            if (strcmp(argv[i], "-o") == 0) {
                output = argv[i + 1];
            }
        }
        if (!output) {
            return fail("project render needs -o OUT.ppm");
        }
        size_t length = 0;
        char *text = read_file(document, &length);
        if (!text) {
            return fail("cannot read '%s'", document);
        }
        jfx_project_kind_t kind = JFX_PROJECT_KIND_SEQUENCE;
        char error[256] = { 0 };
        const jfx_result_t detected =
            jfx_project_kind_of(text, length, &kind, error, sizeof(error));
        tilly_mem_free(text);
        if (detected != JFX_SUCCESS) {
            return fail("%s", error);
        }
        if (kind == JFX_PROJECT_KIND_GRAPH) {
            return render_graph_document(document, output, 0, 0, 0.0f);
        }
        if (kind==JFX_PROJECT_KIND_SCENE3D) {
            text=read_file(document,&length); if (!text) return fail("cannot read '%s'",document);
            jfx_editor_t *editor=jfx_editor_create(960,540);
            jfx_result_t status=editor?jfx_editor_load(editor,text,length,error,sizeof(error)):JFX_ERROR_OUT_OF_MEMORY;
            tilly_mem_free(text);
            if (status==JFX_SUCCESS) status=jfx_editor_write_frame(editor,0,960,540,output);
            jfx_editor_destroy(editor);
            return status==JFX_SUCCESS?0:fail("cannot render scene '%s': %s",document,error);
        }
        char path_copy[MAX_PATH];
        snprintf(path_copy, sizeof(path_copy), "%s", document);
        char *forwarded[3] = { path_copy, (char *)"-o", (char *)output };
        return cmd_render_sequence(3, forwarded);
    }
    return fail("unknown project subcommand '%s'", sub);
}
