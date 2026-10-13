#include "typed_internal.h"
#include "bytecode_internal.h"
#include "tilly/containers.h"
#include <ctype.h>
#include <stdio.h>

typedef struct {
    const char *s;
    size_t pos, line, column;
    jt_node nodes[JT_NODES];
    uint32_t count;
    kvec_t(uint8_t) pool;
    jt_error *error;
    jolt_status_t status;
} reader;
bool jt_source_typed(const char *source) {
    const char *p=source;
    while(*p) {
        if(*p==';') { while(*p&&*p!='\n') ++p; continue; }
        if(*p=='"'||*p=='\\') return true;
        if(isspace((unsigned char)*p)||strchr("()[]",*p)) { ++p;continue; }
        const char *start=p;
        while(*p&&!isspace((unsigned char)*p)&&!strchr("()[];",*p)) ++p;
        size_t len=(size_t)(p-start);
        const char *words[]={"import","module","intrinsic","export","for","while","loop","break","continue","fn","defn","->","true","false","void","i32","i64","u8","u16","u32","u64","f32","f64","bool","char","string","value","function","list","vector","set"};
        for(size_t i=0;i<sizeof(words)/sizeof(*words);++i) if(len==strlen(words[i])&&!memcmp(start,words[i],len)) return true;
        if(isdigit((unsigned char)*start)||*start=='-'||*start=='+') {
            const char *suffixes[]={"i32","i64","u8","u16","u32","u64","f32","f64"};
            for(size_t i=0;i<8;++i) { size_t n=strlen(suffixes[i]);if(len>n&&!memcmp(p-n,suffixes[i],n)) return true; }
        }
    }
    return false;
}
static bool error(reader *r, jolt_status_t status, const char *message) {
    if (!r->status) {
        r->status=status;
        if (r->error) {
            r->error->line=r->line; r->error->column=r->column;
            snprintf(r->error->message,sizeof(r->error->message),"%s",message);
        }
    }
    return false;
}
static void advance(reader *r) {
    if(r->s[r->pos++]=='\n') { ++r->line; r->column=1; } else ++r->column;
}
static void whitespace(reader *r) {
    for (;;) {
        while(isspace((unsigned char)r->s[r->pos])) advance(r);
        if(r->s[r->pos]!=';') break;
        while(r->s[r->pos] && r->s[r->pos]!='\n') advance(r);
    }
}
static uint32_t form(reader *r, unsigned depth) {
    whitespace(r);
    if(depth>=JT_DEPTH || r->count==JT_NODES) {
        error(r,JOLT_ERR_BUDGET,"typed syntax budget exceeded"); return 0;
    }
    uint32_t id=r->count++;
    jt_node *n=&r->nodes[id];
    n->line=(uint32_t)r->line; n->column=(uint32_t)r->column;
    char ch=r->s[r->pos];
    if(ch=='(' || ch=='[') {
        n->tag=ch=='(' ? 1u : 2u;
        char end=ch=='(' ? ')' : ']'; advance(r);
        uint32_t last=0;
        for (;;) {
            whitespace(r);
            if(r->s[r->pos]==end) { advance(r); return id; }
            if(!r->s[r->pos] || r->s[r->pos]==')' || r->s[r->pos]==']') {
                error(r,JOLT_ERR_SYNTAX,"unclosed or mismatched delimiter"); return 0;
            }
            uint32_t child=form(r,depth+1); if(!child) return 0;
            if(last) r->nodes[last].next=child; else n->child=child;
            last=child;
        }
    }
    size_t start=r->pos;
    n->tag=ch=='"' ? 3u : 0u;
    if(ch=='"') {
        advance(r);
        while(r->s[r->pos] && r->s[r->pos]!='"') {
            if(r->s[r->pos]=='\\') { advance(r); if(!r->s[r->pos]) break; }
            advance(r);
        }
        if(r->s[r->pos]!='"') { error(r,JOLT_ERR_SYNTAX,"unterminated string"); return 0; }
        advance(r);
    } else {
        while(r->s[r->pos] && !isspace((unsigned char)r->s[r->pos]) && !strchr("()[];",r->s[r->pos])) advance(r);
    }
    size_t length=r->pos-start;
    if(!length) { error(r,JOLT_ERR_SYNTAX,"expected expression"); return 0; }
    if(!tilly_vec_reserve(&r->pool,r->pool.n+length)) { error(r,JOLT_ERR_MEMORY,"allocation failed"); return 0; }
    n->text=(uint32_t)r->pool.n; n->length=(uint32_t)length;
    memcpy(r->pool.a+r->pool.n,r->s+start,length); r->pool.n+=length;
    return id;
}
jolt_status_t jt_compile(const char *source, uint8_t **code, size_t *size, jt_error *e) {
    *code=NULL; *size=0;
    if(strlen(source)>1024u*1024u) return JOLT_ERR_BUDGET;
    reader *r=tilly_container_calloc(1,sizeof(*r));
    if(!r) return JOLT_ERR_MEMORY;
    r->s=source; r->count=1; r->line=r->column=1; r->error=e;
    uint32_t root=0,last=0;
    while(!r->status) {
        whitespace(r); if(!source[r->pos]) break;
        uint32_t n=form(r,0); if(!n) break;
        if(last) r->nodes[last].next=n; else root=n;
        last=n;
    }
    if(!root && !r->status) error(r,JOLT_ERR_SYNTAX,"expected defkernel");
    uint8_t *bytes=NULL;
    size_t length=JT_HEADER+(size_t)r->count*JT_NODE+r->pool.n;
    if(!r->status) {
        bytes=tilly_container_calloc(1,length);
        if(!bytes) error(r,JOLT_ERR_MEMORY,"allocation failed");
    }
    if(!r->status) {
        jolt_write_u32(bytes,JOLT_BYTECODE_MAGIC); jolt_write_u32(bytes+4,2);
        jolt_write_u32(bytes+16,r->count); jolt_write_u32(bytes+20,(uint32_t)r->pool.n);
        jolt_write_u32(bytes+24,root);
        for(uint32_t i=0;i<r->count;++i) {
            jt_node n=r->nodes[i];
            uint32_t words[]={n.tag,n.child,n.next,n.text,n.length,n.line,n.column};
            for(unsigned j=0;j<7;++j) jolt_write_u32(bytes+JT_HEADER+(size_t)i*JT_NODE+j*4,words[j]);
        }
        if(r->pool.n) memcpy(bytes+JT_HEADER+(size_t)r->count*JT_NODE,r->pool.a,r->pool.n);
        /* The initial header is filled from the checked kernel signature. */
        r->status=jt_prepare(bytes,length,e);
        if(!r->status) { *code=bytes; *size=length; bytes=NULL; }
    }
    jolt_status_t status=r->status;
    tilly_container_free(bytes); tilly_vec_destroy(r->pool); tilly_container_free(r);
    return status;
}
