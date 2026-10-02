#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "jfx/jfx_editor.h"

static jfx_result_t edit(jfx_editor_t *e,const char *op,uint32_t a,uint32_t b,uint32_t c,double v,const char *s) {
    return jfx_editor_command(e,op,a,b,c,v,s);
}
static void persistence(void) {
    const char *doc="size 2 1\nnode solid \"Output #1\"\nnode color \"Red source\"\nparam 2 g 0\nparam 2 b 0\nlink 2 0 -> 1 0\noutput 1\n";
    jfx_editor_t *e=jfx_editor_create(2,1); assert(e);
    assert(jfx_editor_load(e,doc,strlen(doc),NULL,0)==JFX_SUCCESS);
    assert(jfx_editor_output(e)==0);
    assert(!strcmp(jfx_graph_node_label(jfx_editor_graph(e),0),"Output #1"));
    unsigned char pixels[8]; assert(jfx_editor_render(e,0,2,1,pixels,8)==JFX_SUCCESS);
    assert(pixels[0]==255 && pixels[1]==0 && pixels[3]==255);
    assert(edit(e,"node.position",0,0,0,-12.25,"123.5")==JFX_SUCCESS);
    assert(edit(e,"node.add",0,0,0,0,"image")==JFX_SUCCESS);
    assert(edit(e,"node.path",2,0,0,0,"/a path #1 with \"quotes\".png")==JFX_SUCCESS);
    char saved[16384]; size_t n;
    assert(jfx_editor_save(e,saved,sizeof(saved),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,saved,n,NULL,0)==JFX_SUCCESS);
    float x,y; assert(jfx_graph_node_position(jfx_editor_graph(e),0,&x,&y)==JFX_SUCCESS);
    assert(x==-12.25f && y==123.5f);
    assert(jfx_editor_output(e)==0);
    assert(!strcmp(jfx_graph_node_string(jfx_editor_graph(e),2,0),"/a path #1 with \"quotes\".png"));
    assert(edit(e,"node.path",2,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_save(e,saved,sizeof(saved),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,saved,n,NULL,0)==JFX_SUCCESS);
    assert(jfx_graph_node_string(jfx_editor_graph(e),2,0) && !*jfx_graph_node_string(jfx_editor_graph(e),2,0));
    assert(edit(e,"node.label",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_save(e,saved,sizeof(saved),&n)==JFX_SUCCESS);
    assert(jfx_editor_load(e,saved,n,NULL,0)==JFX_SUCCESS);
    assert(!*jfx_graph_node_label(jfx_editor_graph(e),0));
    const char *oversized="graph\nnode solid \"0123456789012345678901234567890123456789012345678901234567890123456789\"\n";
    assert(jfx_editor_load(e,oversized,strlen(oversized),NULL,0)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_editor_destroy(e);
}
static void history(void) {
    jfx_editor_t *e=jfx_editor_create(2,1); assert(e);
    assert(edit(e,"clip.name",0,0,0,0,"Sequence kept")==JFX_SUCCESS);
    assert(edit(e,"graph.new",2,1,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.add",0,0,0,0,"color")==JFX_SUCCESS);
    assert(edit(e,"node.param",1,0,0,0.123456791,"g")==JFX_SUCCESS);
    float value=jfx_graph_node_value(jfx_editor_graph(e),1)->scalars[1];
    assert(edit(e,"node.connect",1,0,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.duplicate",1,0,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.param",2,0,0,0,"g")==JFX_SUCCESS);
    assert(jfx_graph_node_value(jfx_editor_graph(e),1)->scalars[1]==value);
    assert(edit(e,"node.label",2,0,0,0,"Copied color")==JFX_SUCCESS);
    assert(edit(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(!strcmp(jfx_graph_node_label(jfx_editor_graph(e),2),"Copied color"));
    assert(jfx_graph_node_value(jfx_editor_graph(e),1)->scalars[1]==value);
    assert(edit(e,"node.remove",2,0,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.remove",1,0,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.remove",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_output(e)==UINT32_MAX);
    char doc[8192]; size_t n;
    assert(jfx_editor_save(e,doc,sizeof(doc),&n)==JFX_SUCCESS);
    assert(edit(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_output(e)==0);
    assert(edit(e,"redo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_graph_node_count(jfx_editor_graph(e))==0);
    assert(jfx_editor_load(e,doc,n,NULL,0)==JFX_SUCCESS);
    assert(!jfx_editor_can_undo(e));
    assert(!strcmp(jfx_timeline_clip_name(jfx_editor_timeline(e),0,0),"Sequence kept"));
    assert(edit(e,"clip.name",0,0,0,0,"Changed while graph active")==JFX_SUCCESS);
    assert(edit(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_kind(e)==JFX_PROJECT_KIND_GRAPH);
    assert(!strcmp(jfx_timeline_clip_name(jfx_editor_timeline(e),0,0),"Sequence kept"));
    jfx_editor_destroy(e);
}
static void validation(void) {
    jfx_editor_t *e=jfx_editor_create(2,1); assert(e);
    assert(edit(e,"graph.new",2,1,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.add",0,0,0,0,"exposure")==JFX_SUCCESS);
    assert(edit(e,"node.connect",0,1,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.add",0,0,0,0,"invert")==JFX_SUCCESS);
    assert(edit(e,"node.connect",1,2,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.connect",2,1,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_input_source(jfx_editor_graph(e),1,0)==0);
    assert(edit(e,"node.add",0,0,0,0,"color")==JFX_SUCCESS);
    assert(edit(e,"node.connect",3,1,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"node.disconnect",3,0,0,0,"")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"node.position",0,0,0,0,"nan")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"node.position",0,0,0,0,"1 extra")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"node.position",0,0,0,1.e6+0.001,"0")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"node.position",0,0,0,0,"1000000.001")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"node.add",0,0,0,0,"checker")==JFX_SUCCESS);
    assert(edit(e,"node.param",4,0,0,2.5,"size")==JFX_ERROR_INVALID_ARGUMENT);
    assert(edit(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_can_redo(e));
    assert(edit(e,"node.param",1,0,0,100,"stops")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_can_redo(e));
    assert(jfx_editor_load(e,"garbage",7,NULL,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_can_redo(e));
    char json[65536];
    assert(jfx_editor_graph_state(e,json,sizeof(json))==JFX_SUCCESS);
    assert(strstr(json,"\"kind\":\"exposure\"") && strstr(json,"\"source\":0"));
    assert(jfx_editor_graph_state(e,json,1)==JFX_ERROR_OUT_OF_MEMORY);
    assert(jfx_editor_graph_state(NULL,json,sizeof(json))==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_node_catalog(json,sizeof(json))==JFX_SUCCESS);
    assert(strstr(json,"\"name\":\"blend\"") && strstr(json,"\"type\":\"color\"") && strstr(json,"grade_primary"));
    assert(jfx_node_catalog(json,1)==JFX_ERROR_OUT_OF_MEMORY);
    assert(jfx_node_catalog(NULL,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_graph_width(NULL)==0 && jfx_editor_graph_height(NULL)==0);
    assert(jfx_editor_write_graph(NULL,0,0,1,1,"unused")==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_render_graph(NULL,0,0,1,1,(uint8_t *)json,sizeof(json))==JFX_ERROR_INVALID_ARGUMENT);
    uint32_t out=99;
    assert(jfx_graph_duplicate_node(NULL,0,&out)==JFX_ERROR_INVALID_ARGUMENT && out==99);
    assert(jfx_graph_node_position(NULL,0,NULL,NULL)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_set_node_position(jfx_editor_graph(e),0,NAN,0)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_graph_set_node_param(NULL,0,0,0)==JFX_ERROR_INVALID_ARGUMENT);
    jfx_editor_destroy(e);
}
static void ownership_limits_and_export(const char *path) {
    jfx_editor_t *e=jfx_editor_create(1,1); assert(e);
    assert(edit(e,"graph.new",1,1,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.add",0,0,0,0,"image")==JFX_SUCCESS);
    assert(edit(e,"node.path",1,0,0,0,"/missing/first-image.png")==JFX_SUCCESS);
    assert(edit(e,"node.duplicate",1,0,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.path",2,0,0,0,"/missing/copy-image.png")==JFX_SUCCESS);
    assert(!strcmp(jfx_graph_node_string(jfx_editor_graph(e),1,0),"/missing/first-image.png"));
    uint8_t pixels[4]={1,2,3,4};
    assert(jfx_editor_render_graph(e,UINT32_MAX,0,1,1,pixels,4)==JFX_SUCCESS && pixels[0]==255);
    assert(edit(e,"sequence",0,0,0,0,"")==JFX_SUCCESS);
    assert(jfx_editor_render_graph(e,0,2,1,1,pixels,4)==JFX_SUCCESS && jfx_editor_kind(e)==JFX_PROJECT_KIND_SEQUENCE);
    assert(jfx_editor_render_graph(e,1,0,1,1,pixels,4)!=JFX_SUCCESS && pixels[0]==255);
    assert(jfx_editor_render_graph(e,0,NAN,1,1,pixels,4)==JFX_ERROR_INVALID_ARGUMENT);
    assert(jfx_editor_render_graph(e,0,0,1,1,pixels,3)==JFX_ERROR_INVALID_ARGUMENT);
    FILE *file=fopen(path,"wb"); assert(file && fwrite("keep",1,4,file)==4 && !fclose(file));
    assert(jfx_editor_write_graph(e,1,0,1,1,path)!=JFX_SUCCESS);
    char kept[8]={0}; file=fopen(path,"rb"); assert(file && fread(kept,1,sizeof(kept),file)==4 && !fclose(file));
    assert(!strcmp(kept,"keep")); assert(!remove(path));
    assert(jfx_editor_write_graph(e,0,0,1,1,path)==JFX_SUCCESS); assert(!remove(path));
    const char *no_output="graph\nnode solid\noutput 0\n";
    assert(jfx_editor_load(e,no_output,strlen(no_output),NULL,0)==JFX_SUCCESS);
    assert(jfx_editor_render_graph(e,UINT32_MAX,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==0);
    assert(jfx_editor_render_graph(e,0,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==255);
    assert(edit(e,"node.add",0,0,0,0,"video")==JFX_SUCCESS);
    assert(jfx_editor_render_graph(e,1,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==0);
    assert(edit(e,"node.add",0,0,0,0,"image")==JFX_SUCCESS);
    assert(jfx_editor_render_graph(e,2,0,1,1,pixels,4)==JFX_SUCCESS && pixels[3]==0);
    assert(edit(e,"sequence",0,0,0,0,"")==JFX_SUCCESS);
    jfx_graph_t *g=jfx_editor_graph(e); uint32_t n;
    while (jfx_graph_node_count(g)<JFX_GRAPH_MAX_NODES) assert(jfx_graph_add_node(g,"solid",NULL,&n)==JFX_SUCCESS);
    jfx_editor_clear_history(e); n=99;
    assert(jfx_graph_duplicate_node(g,1,&n)==JFX_ERROR_OUT_OF_MEMORY && n==99);
    assert(edit(e,"node.duplicate",1,0,0,0,"")==JFX_ERROR_OUT_OF_MEMORY && !jfx_editor_can_undo(e));
    assert(jfx_editor_render_graph(e,0,0,1,1,pixels,4)==JFX_SUCCESS && pixels[0]==255);
    assert(edit(e,"graph.new",2,1,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"undo",0,0,0,0,"")==JFX_SUCCESS && jfx_editor_kind(e)==JFX_PROJECT_KIND_SEQUENCE);
    assert(jfx_graph_node_count(jfx_editor_graph(e))==JFX_GRAPH_MAX_NODES);
    assert(edit(e,"redo",0,0,0,0,"")==JFX_SUCCESS && jfx_editor_kind(e)==JFX_PROJECT_KIND_GRAPH);
    for (int i=0;i<40;++i) assert(edit(e,"node.label",0,0,0,0,"Rename")==JFX_SUCCESS);
    for (int i=0;i<32;++i) assert(edit(e,"undo",0,0,0,0,"")==JFX_SUCCESS);
    assert(!jfx_editor_can_undo(e) && jfx_editor_can_redo(e));
    jfx_editor_destroy(e);
}
static void frame_budget(void) {
    jfx_graph_t *g=jfx_graph_create(); uint32_t a,b,c; assert(g);
    assert(jfx_graph_add_node(g,"solid",NULL,&a)==JFX_SUCCESS);
    assert(jfx_graph_add_node(g,"invert",NULL,&b)==JFX_SUCCESS);
    assert(jfx_graph_add_node(g,"opacity",NULL,&c)==JFX_SUCCESS);
    assert(jfx_graph_connect(g,a,0,b,0)==JFX_SUCCESS && jfx_graph_connect(g,b,0,c,0)==JFX_SUCCESS);
    uint8_t sentinel[4]={1,2,3,4};
    /* Preflight rejects the 768-MiB reachable frame set before touching pixels. */
    assert(jfx_graph_render(g,c,4096,4096,0,sentinel)==JFX_ERROR_OUT_OF_MEMORY && sentinel[0]==1);
    jfx_image_t image={.size=sizeof(image)};
    assert(jfx_graph_render_node(g,c,4096,4096,0,&image)==JFX_ERROR_OUT_OF_MEMORY && !image.pixels);
    jfx_graph_destroy(g);
}
static void image_sampling(const char *path) {
    const unsigned char rgb[]={200,100,50, 20,40,80, 0,255,0, 0,0,255};
    const unsigned char rgba[]={200,100,50,255, 20,40,80,255, 0,255,0,255, 0,0,255,255};
    FILE *file=fopen(path,"wb"); assert(file);
    assert(fputs("P6\n2 2\n255\n",file)>=0 && fwrite(rgb,1,sizeof(rgb),file)==sizeof(rgb) && !fclose(file));
    jfx_editor_t *e=jfx_editor_create(2,2); assert(e);
    assert(edit(e,"graph.new",2,2,0,0,"")==JFX_SUCCESS);
    assert(edit(e,"node.add",0,0,0,0,"image")==JFX_SUCCESS);
    assert(edit(e,"node.path",1,0,0,0,path)==JFX_SUCCESS);
    assert(edit(e,"node.output",1,0,0,0,"")==JFX_SUCCESS);
    unsigned char pixels[64];
    assert(jfx_editor_render(e,0,2,2,pixels,sizeof(pixels))==JFX_SUCCESS);
    assert(!memcmp(pixels,rgba,sizeof(rgba))); // includes right and bottom edges
    assert(jfx_editor_render(e,0,4,4,pixels,sizeof(pixels))==JFX_SUCCESS);
    for (unsigned y=0;y<4;++y) for (unsigned x=0;x<4;++x)
        assert(!memcmp(pixels+(y*4+x)*4,rgba+(y/2*2+x/2)*4,4));
    assert(jfx_editor_render(e,0,1,1,pixels,sizeof(pixels))==JFX_SUCCESS);
    assert(!memcmp(pixels,rgba,4));
    jfx_editor_destroy(e); assert(!remove(path));
}
int main(int argc,char **argv) { assert(argc==2); persistence(); history(); validation(); ownership_limits_and_export(argv[1]); frame_budget(); image_sampling(argv[1]); return 0; }
