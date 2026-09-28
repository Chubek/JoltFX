#include "commands.h"
#include "jfx/jfx_editor.h"
#include "tilly/allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* A line-oriented terminal editor: the same command surface used by the
 * browser/mobile panels. Commands can also be piped for reproducible edits. */
int cmd_edit(int argc, char **argv) {
    if (argc!=2) { fprintf(stderr,"usage: joltfx edit INPUT.jfx OUTPUT.jfx\n"); return 1; }
    FILE *input=fopen(argv[0],"rb");
    if (!input) { perror(argv[0]); return 1; }
    char *doc=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),JFX_PROJECT_MAX_BYTES+1,_Alignof(max_align_t));
    jfx_editor_t *e=jfx_editor_create(320,180);
    if (!doc || !e) { fclose(input); jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),doc); return 1; }
    size_t n=fread(doc,1,JFX_PROJECT_MAX_BYTES+1,input); int failed=ferror(input); fclose(input);
    char error[256]={0}; int exit_code=1;
    if (failed || jfx_editor_load(e,doc,n,error,sizeof(error))!=JFX_SUCCESS) { fprintf(stderr,"%s\n",error); goto done; }
    fprintf(stderr,"JoltFX editor: NLE | Layer Effects | Color Grading | Node Compositing\n"
        "Commands: OP A B C VALUE TEXT (zero-based indices); show, save, quit. See docs/editor.md.\n");
    char line[2048];
    while (fgets(line,sizeof(line),stdin)) {
        if (strcmp(line,"quit\n")==0) { exit_code=0; goto done; }
        if (strcmp(line,"save\n")==0) break;
        if (strcmp(line,"show\n")==0) {
            if (jfx_editor_save(e,doc,JFX_PROJECT_MAX_BYTES,&n)==JFX_SUCCESS) puts(doc);
            continue;
        }
        char op[64],text[512]={0}; unsigned a,b,c; double value;
        int count=sscanf(line,"%63s %u %u %u %lf %511[^\n]",op,&a,&b,&c,&value,text);
        if (count<5 || jfx_editor_command(e,op,a,b,c,value,text)!=JFX_SUCCESS) {
            fprintf(stderr,"Edit rejected; document unchanged by this command.\n"); goto done;
        }
    }
    if (jfx_editor_save(e,doc,JFX_PROJECT_MAX_BYTES,&n)!=JFX_SUCCESS) goto done;
    FILE *output=fopen(argv[1],"wb");
    if (!output) { perror(argv[1]); goto done; }
    failed=fwrite(doc,1,n,output)!=n;
    if (fclose(output)) failed=1;
    exit_code=failed?1:0;
done:
    jfx_editor_destroy(e); tilly_free((tilly_allocator_t *)tilly_default_allocator(),doc); return exit_code;
}
