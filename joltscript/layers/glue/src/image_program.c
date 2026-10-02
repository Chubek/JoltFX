/* A bounded scalar AST interpreter for frame Joltscript. This supplies language
 * mechanics and read-only sampling; effect algorithms belong to .jolt sources. */
#include "joltscript/image_program.h"
#include "tilly/containers.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define NODE_LIMIT 8192
#define FUNCTION_LIMIT 160
#define PARAM_LIMIT 64
#define LOCAL_LIMIT 512
#define ARG_LIMIT 32
#define DEPTH_LIMIT 128

typedef struct { char text[64]; double value; int child, next, number, list; } node_t;
typedef struct { const char *name; int args, body; size_t arity; } function_t;
struct jolt_image_program {
    node_t nodes[NODE_LIMIT]; int count;
    function_t functions[FUNCTION_LIMIT]; size_t function_count;
    jolt_image_parameter_info_t params[PARAM_LIMIT]; size_t param_count;
    int entry, passes;
};
typedef struct {
    jolt_image_program_t *p; const char *start, *cursor;
    jolt_diagnostic_t *d; jolt_status_t status;
} reader_t;
static int parse_error(reader_t *r, const char *message) {
    r->status=JOLT_ERR_SYNTAX;
    if(r->d) {
        r->d->line=1; r->d->column=1;
        for(const char *s=r->start;s<r->cursor;++s) {
            if(*s=='\n') { ++r->d->line; r->d->column=1; } else ++r->d->column;
        }
        snprintf(r->d->message,sizeof(r->d->message),"%s",message);
    }
    return 0;
}
static void whitespace(reader_t *r) {
    for(;;) {
        while(isspace((unsigned char)*r->cursor)) ++r->cursor;
        if(*r->cursor!=';') return;
        while(*r->cursor && *r->cursor!='\n') ++r->cursor;
    }
}
static int read_node(reader_t *r, unsigned depth) {
    whitespace(r);
    if(depth>=64 || r->p->count==NODE_LIMIT-1) return parse_error(r,"source complexity limit");
    int id=++r->p->count; node_t *n=&r->p->nodes[id];
    if(*r->cursor=='(' || *r->cursor=='[') {
        char end=*r->cursor++=='(' ? ')' : ']'; n->list=1;
        int *next=&n->child;
        for(;;) {
            whitespace(r);
            if(*r->cursor==end) { ++r->cursor; return id; }
            if(!*r->cursor || *r->cursor==')' || *r->cursor==']') return parse_error(r,"unclosed or mismatched list");
            int child=read_node(r,depth+1); if(!child) return 0;
            *next=child; next=&r->p->nodes[child].next;
        }
    }
    size_t len=0;
    while(*r->cursor && !isspace((unsigned char)*r->cursor) && !strchr("()[];",*r->cursor)) {
        if(len==63) return parse_error(r,"symbol exceeds 63 bytes");
        n->text[len++]=*r->cursor++;
    }
    if(!len) return parse_error(r,"expected symbol or number");
    char *end; errno=0; n->value=strtod(n->text,&end);
    n->number=end!=n->text && !*end && !errno && isfinite(n->value);
    return id;
}
static size_t length(const jolt_image_program_t *p, int id) {
    size_t n=0; for(;id;id=p->nodes[id].next) ++n; return n;
}
static int next(const jolt_image_program_t *p,int id) { return p->nodes[id].next; }
static int lookup_function(const jolt_image_program_t *p,const char *name) {
    for(size_t i=0;i<p->function_count;++i) if(!strcmp(name,p->functions[i].name)) return (int)i;
    return -1;
}
static int builtin_index(const char *s);
static int register_form(reader_t *r,int id) {
    jolt_image_program_t *p=r->p;
    int head=p->nodes[id].child, a=next(p,head);
    if(!p->nodes[id].list || !head) return parse_error(r,"expected top-level form");
    const char *kind=p->nodes[head].text;
    if(!strcmp(kind,"param")) {
        if(length(p,a)!=5 || p->param_count==PARAM_LIMIT) return parse_error(r,"param: name default min max integer");
        int b=next(p,a),c=next(p,b),d=next(p,c),e=next(p,d);
        if(p->nodes[a].list || p->nodes[a].number || !p->nodes[b].number || !p->nodes[c].number || !p->nodes[d].number || !p->nodes[e].number)
            return parse_error(r,"invalid parameter declaration");
        double def=p->nodes[b].value,lo=p->nodes[c].value,hi=p->nodes[d].value,integer=p->nodes[e].value;
        if(lo>def || hi<def || (integer!=0 && integer!=1) || (integer!=0 && (floor(def)!=def || floor(lo)!=lo || floor(hi)!=hi)))
            return parse_error(r,"invalid parameter range");
        for(size_t i=0;i<p->param_count;++i) if(!strcmp(p->params[i].name,p->nodes[a].text)) return parse_error(r,"duplicate parameter");
        p->params[p->param_count++]=(jolt_image_parameter_info_t){p->nodes[a].text,def,lo,hi,(int)integer};
    } else if(!strcmp(kind,"defn") || !strcmp(kind,"defkernel")) {
        if(length(p,a)!=3) return parse_error(r,"expected function name, bindings and body");
        if(p->function_count==FUNCTION_LIMIT) return parse_error(r,"function limit exceeded");
        int args=next(p,a),body=next(p,args);
        size_t arity=length(p,p->nodes[args].child);
        if(p->nodes[a].list || p->nodes[a].number || !p->nodes[args].list || arity>ARG_LIMIT || lookup_function(p,p->nodes[a].text)>=0 ||
            builtin_index(p->nodes[a].text)>=0 || !strcmp(p->nodes[a].text,"if") ||
            !strcmp(p->nodes[a].text,"let") || !strcmp(p->nodes[a].text,"sum"))
            return parse_error(r,"invalid or duplicate function");
        for(int b=p->nodes[args].child;b;b=next(p,b)) {
            if(p->nodes[b].list || p->nodes[b].number) return parse_error(r,"invalid binding");
            for(int c=next(p,b);c;c=next(p,c)) if(!strcmp(p->nodes[b].text,p->nodes[c].text)) return parse_error(r,"duplicate binding");
        }
        if(!strcmp(kind,"defkernel")) {
            if(p->entry>=0 || arity!=3) return parse_error(r,"one kernel with [x y c] required");
            p->entry=(int)p->function_count;
        }
        p->functions[p->function_count++]=(function_t){p->nodes[a].text,p->nodes[args].child,body,arity};
    } else if(!strcmp(kind,"passes")) {
        if(length(p,a)!=1 || p->passes) return parse_error(r,"one passes expression required");
        p->passes=a;
    } else return parse_error(r,"unknown top-level form");
    return 1;
}
static const struct { const char *name; int arity; } builtins[]={
    {"+",-1},{"-",-1},{"*",-1},{"/",2},{"min",2},{"max",2},{"pow",2},
    {"sin",1},{"cos",1},{"tan",1},{"atan2",2},{"exp",1},{"log",1},
    {"sqrt",1},{"abs",1},{"floor",1},{"ceil",1},{"round",1},{"mod",2},
    {"<",2},{">",2},{"<=",2},{">=",2},{"=",2},{"!=",2},{"and",2},{"or",2},{"not",1},
    {"sample",5},{"data",1},{"data-count",0},{"require",1}
};
static int builtin_index(const char *s) {
    for(size_t i=0;i<sizeof(builtins)/sizeof(*builtins);++i) if(!strcmp(s,builtins[i].name)) return (int)i;
    return -1;
}
typedef struct { const char *names[LOCAL_LIMIT]; size_t count; } scope_t;
static int validate(reader_t *r,int id,unsigned depth,scope_t *scope) {
    jolt_image_program_t *p=r->p; node_t *n=&p->nodes[id];
    if(depth>=64 || !id) return parse_error(r,"invalid expression depth");
    if(!n->list) {
        if(n->number || !strcmp(n->text,"width") || !strcmp(n->text,"height") ||
            !strcmp(n->text,"true") || !strcmp(n->text,"false") || !strcmp(n->text,"pi") || !strcmp(n->text,"pass")) return 1;
        for(size_t i=0;i<scope->count;++i) if(!strcmp(n->text,scope->names[i])) return 1;
        for(size_t i=0;i<p->param_count;++i) if(!strcmp(n->text,p->params[i].name)) return 1;
        return parse_error(r,"undefined binding");
    }
    int head=n->child,a=next(p,head); const char *op=p->nodes[head].text;
    size_t count=length(p,a),mark=scope->count;
    if(!strcmp(op,"let")) {
        if(count!=2 || !p->nodes[a].list || length(p,p->nodes[a].child)%2) return parse_error(r,"let requires paired bindings and body");
        for(int b=p->nodes[a].child;b;) {
            if(p->nodes[b].list || p->nodes[b].number || scope->count==LOCAL_LIMIT) return parse_error(r,"invalid local binding");
            int value=next(p,b);
            if(!validate(r,value,depth+1,scope)) return 0;
            scope->names[scope->count++]=p->nodes[b].text;
            b=next(p,value);
        }
        int result=validate(r,next(p,a),depth+1,scope); scope->count=mark; return result;
    }
    if(!strcmp(op,"sum")) {
        if(count!=4 || p->nodes[a].list || p->nodes[a].number || scope->count==LOCAL_LIMIT) return parse_error(r,"sum requires name start end expression");
        int lo=next(p,a),hi=next(p,lo),body=next(p,hi);
        if(!validate(r,lo,depth+1,scope) || !validate(r,hi,depth+1,scope)) return 0;
        scope->names[scope->count++]=p->nodes[a].text;
        int result=validate(r,body,depth+1,scope); scope->count=mark; return result;
    } else if(!strcmp(op,"if")) {
        if(count!=3) return parse_error(r,"if requires three expressions");
    } else {
        int b=builtin_index(op),f=lookup_function(p,op);
        if(b<0 && f<0) return parse_error(r,"unknown function");
        int arity=b>=0 ? builtins[b].arity : (int)p->functions[f].arity;
        if((arity>=0 && count!=(size_t)arity) || (arity<0 && !count) || count>ARG_LIMIT) return parse_error(r,"wrong function arity");
    }
    for(;a;a=next(p,a)) if(!validate(r,a,depth+1,scope)) return 0;
    return 1;
}
jolt_status_t jolt_image_compile(const char *library,const char *source,jolt_image_program_t **out,jolt_diagnostic_t *d) {
    if(out) *out=NULL;
    if(!library || !source || !out || (d && d->size!=sizeof(*d))) return JOLT_ERR_ARGUMENT;
    if(d) { d->line=d->column=0; d->message[0]=0; }
    const char *sources[]={library,source};
    for(size_t i=0;i<2;++i) {
        size_t n=0; while(sources[i][n] && n<=1024*1024) ++n;
        if(n>1024*1024) return JOLT_ERR_BUDGET;
    }
    jolt_image_program_t *p=tilly_container_calloc(1,sizeof(*p)); if(!p) return JOLT_ERR_MEMORY;
    p->entry=-1;
    reader_t r={.p=p,.d=d};
    for(size_t i=0;i<2 && r.status==JOLT_OK;++i) {
        r.start=r.cursor=sources[i];
        for(;;) {
            whitespace(&r); if(!*r.cursor) break;
            int id=read_node(&r,0); if(!id || !register_form(&r,id)) break;
        }
    }
    if(r.status==JOLT_OK && p->entry<0) parse_error(&r,"missing defkernel");
    for(size_t i=0;i<p->function_count && r.status==JOLT_OK;++i) {
        scope_t scope={0};
        for(int arg=p->functions[i].args;arg;arg=next(p,arg)) scope.names[scope.count++]=p->nodes[arg].text;
        validate(&r,p->functions[i].body,0,&scope);
    }
    if(r.status==JOLT_OK && p->passes) { scope_t scope={0}; validate(&r,p->passes,0,&scope); }
    if(r.status!=JOLT_OK) { tilly_container_free(p); return r.status; }
    *out=p; return JOLT_OK;
}
void jolt_image_program_destroy(jolt_image_program_t *p) { tilly_container_free(p); }
size_t jolt_image_parameter_count(const jolt_image_program_t *p) { return p ? p->param_count : 0; }
const jolt_image_parameter_info_t *jolt_image_parameter_info(const jolt_image_program_t *p,size_t i) {
    return p && i<p->param_count ? &p->params[i] : NULL;
}
typedef struct { const char *name; double value; } binding_t;
typedef struct {
    const jolt_image_program_t *p; const float *src,*data;
    size_t width,height,data_count,steps;
    int pass;
    double params[PARAM_LIMIT]; binding_t locals[LOCAL_LIMIT]; size_t local_count,base;
    jolt_status_t status;
} context_t;
static double error(context_t *ctx,jolt_status_t s) { if(ctx->status==JOLT_OK) ctx->status=s; return 0; }
static double eval(context_t *ctx,int id,unsigned depth);
static double invoke(context_t *ctx,int fn,const double *args,unsigned depth) {
    const function_t *f=&ctx->p->functions[fn];
    size_t mark=ctx->local_count,base=ctx->base;
    if(mark+f->arity>LOCAL_LIMIT) return error(ctx,JOLT_ERR_BUDGET);
    ctx->base=mark; size_t i=0;
    for(int a=f->args;a;a=next(ctx->p,a)) ctx->locals[ctx->local_count++]=(binding_t){ctx->p->nodes[a].text,args[i++]};
    double result=eval(ctx,f->body,depth+1);
    ctx->local_count=mark; ctx->base=base; return result;
}
static double border_index(double x,size_t size,int border) {
    double n=(double)size;
    if(border==1) return fmin(n-1,fmax(0,x));
    if(border==2) { x=fmod(x,n); return x<0 ? x+n : x; }
    if(border==3) { x=fmod(x,2*n); if(x<0) x+=2*n; return x<n ? x : 2*n-1-x; }
    return x;
}
static double texel(context_t *ctx,double x,double y,int c,int border) {
    x=border_index(x,ctx->width,border); y=border_index(y,ctx->height,border);
    if(x<0 || x>=(double)ctx->width || y<0 || y>=(double)ctx->height) return 0;
    return (double)ctx->src[4*((size_t)y*ctx->width+(size_t)x)+(size_t)c];
}
static double sample(context_t *ctx,const double *a) {
    double x=a[0],y=a[1];
    if(a[2]<0 || a[2]>3 || floor(a[2])!=a[2] || a[3]<0 || a[3]>1 || floor(a[3])!=a[3] || a[4]<0 || a[4]>3 || floor(a[4])!=a[4])
        return error(ctx,JOLT_ERR_ARGUMENT);
    int c=(int)a[2],border=(int)a[4];
    if(a[3]==0) return texel(ctx,floor(x+.5),floor(y+.5),c,border);
    double ix=floor(x),iy=floor(y),fx=x-ix,fy=y-iy;
    return (1-fy)*((1-fx)*texel(ctx,ix,iy,c,border)+fx*texel(ctx,ix+1,iy,c,border))+
        fy*((1-fx)*texel(ctx,ix,iy+1,c,border)+fx*texel(ctx,ix+1,iy+1,c,border));
}
static double builtin(context_t *ctx,const char *op,const double *a,size_t n) {
    if(!strcmp(op,"+")) { double v=0; for(size_t i=0;i<n;++i) v+=a[i]; return v; }
    if(!strcmp(op,"*")) { double v=1; for(size_t i=0;i<n;++i) v*=a[i]; return v; }
    if(!strcmp(op,"-")) { double v=a[0]; if(n==1) return -v; for(size_t i=1;i<n;++i) v-=a[i]; return v; }
    if(!strcmp(op,"/")) return a[1]!=0 ? a[0]/a[1] : error(ctx,JOLT_ERR_NUMERIC);
    if(!strcmp(op,"min")) return fmin(a[0],a[1]);
    if(!strcmp(op,"max")) return fmax(a[0],a[1]);
    if(!strcmp(op,"pow")) return pow(a[0],a[1]);
    if(!strcmp(op,"sin")) return sin(a[0]);
    if(!strcmp(op,"cos")) return cos(a[0]);
    if(!strcmp(op,"tan")) return tan(a[0]);
    if(!strcmp(op,"atan2")) return atan2(a[0],a[1]);
    if(!strcmp(op,"exp")) return exp(a[0]);
    if(!strcmp(op,"log")) return log(a[0]);
    if(!strcmp(op,"sqrt")) return sqrt(a[0]);
    if(!strcmp(op,"abs")) return fabs(a[0]);
    if(!strcmp(op,"floor")) return floor(a[0]);
    if(!strcmp(op,"ceil")) return ceil(a[0]);
    if(!strcmp(op,"round")) return round(a[0]);
    if(!strcmp(op,"mod")) return a[1]!=0 ? fmod(a[0],a[1]) : error(ctx,JOLT_ERR_NUMERIC);
    if(!strcmp(op,"<")) return a[0]<a[1];
    if(!strcmp(op,">")) return a[0]>a[1];
    if(!strcmp(op,"<=")) return a[0]<=a[1];
    if(!strcmp(op,">=")) return a[0]>=a[1];
    if(!strcmp(op,"=")) return a[0]==a[1];
    if(!strcmp(op,"!=")) return a[0]!=a[1];
    if(!strcmp(op,"and")) return a[0]!=0 && a[1]!=0;
    if(!strcmp(op,"or")) return a[0]!=0 || a[1]!=0;
    if(!strcmp(op,"not")) return a[0]==0;
    if(!strcmp(op,"require")) return a[0]!=0 ? 1 : error(ctx,JOLT_ERR_ARGUMENT);
    if(!strcmp(op,"sample")) return sample(ctx,a);
    if(!strcmp(op,"data-count")) return (double)ctx->data_count;
    if(!strcmp(op,"data")) {
        if(a[0]<0 || a[0]>=(double)ctx->data_count || floor(a[0])!=a[0]) return error(ctx,JOLT_ERR_ARGUMENT);
        return (double)ctx->data[(size_t)a[0]];
    }
    return error(ctx,JOLT_ERR_SYNTAX);
}
static double eval(context_t *ctx,int id,unsigned depth) {
    if(ctx->status!=JOLT_OK) return 0;
    if(!ctx->steps || depth>=DEPTH_LIMIT) return error(ctx,JOLT_ERR_BUDGET);
    --ctx->steps;
    const jolt_image_program_t *p=ctx->p; const node_t *node=&p->nodes[id];
    if(node->number) return node->value;
    if(!node->list) {
        for(size_t i=ctx->local_count;i>ctx->base;--i) if(!strcmp(node->text,ctx->locals[i-1].name)) return ctx->locals[i-1].value;
        for(size_t i=0;i<p->param_count;++i) if(!strcmp(node->text,p->params[i].name)) return ctx->params[i];
        if(!strcmp(node->text,"pass")) return ctx->pass;
        if(!strcmp(node->text,"width")) return (double)ctx->width;
        if(!strcmp(node->text,"height")) return (double)ctx->height;
        if(!strcmp(node->text,"true")) return 1;
        if(!strcmp(node->text,"false")) return 0;
        if(!strcmp(node->text,"pi")) return 3.14159265358979323846;
        return error(ctx,JOLT_ERR_SYNTAX);
    }
    int head=node->child,a=next(p,head); const char *op=p->nodes[head].text;
    double v=0;
    if(!strcmp(op,"if")) {
        double condition=eval(ctx,a,depth+1); a=next(p,a);
        return eval(ctx,condition!=0 ? a : next(p,a),depth+1);
    }
    if(!strcmp(op,"let")) {
        size_t mark=ctx->local_count;
        for(int b=p->nodes[a].child;b;) {
            int value=next(p,b); double item=eval(ctx,value,depth+1);
            if(ctx->local_count==LOCAL_LIMIT) { error(ctx,JOLT_ERR_BUDGET); break; }
            ctx->locals[ctx->local_count++]=(binding_t){p->nodes[b].text,item}; b=next(p,value);
        }
        v=eval(ctx,next(p,a),depth+1); ctx->local_count=mark;
    } else if(!strcmp(op,"sum")) {
        int start=next(p,a),end=next(p,start),body=next(p,end);
        double lo=eval(ctx,start,depth+1),hi=eval(ctx,end,depth+1);
        if(floor(lo)!=lo || floor(hi)!=hi || hi<lo || hi-lo>65536 || fabs(lo)>1e9 || fabs(hi)>1e9) return error(ctx,JOLT_ERR_ARGUMENT);
        size_t mark=ctx->local_count;
        if(mark==LOCAL_LIMIT) return error(ctx,JOLT_ERR_BUDGET);
        ++ctx->local_count;
        for(double i=lo;i<hi && ctx->status==JOLT_OK;++i) {
            ctx->locals[mark]=(binding_t){p->nodes[a].text,i}; v+=eval(ctx,body,depth+1);
        }
        ctx->local_count=mark;
    } else {
        double args[ARG_LIMIT]; size_t n=0;
        for(;a;a=next(p,a)) args[n++]=eval(ctx,a,depth+1);
        if(ctx->status!=JOLT_OK) return 0;
        int f=lookup_function(p,op);
        v=f>=0 ? invoke(ctx,f,args,depth+1) : builtin(ctx,op,args,n);
    }
    return isfinite(v) ? v : error(ctx,JOLT_ERR_NUMERIC);
}
jolt_status_t jolt_image_program_run(const jolt_image_program_t *p,const float *src,size_t width,size_t height,
    const jolt_image_parameter_t *parameters,size_t parameter_count,const float *data,size_t data_count,
    size_t memory_limit,size_t step_limit,float *dst) {
    if(!p || !src || !dst || !width || !height || width>1048576 || height>1048576 ||
        width>SIZE_MAX/height/(8*sizeof(float)) || (parameter_count && !parameters) || (data_count && !data)) return JOLT_ERR_ARGUMENT;
    size_t values=width*height*4,bytes=values*sizeof(float);
    if(bytes>memory_limit/2 || !step_limit) return JOLT_ERR_BUDGET;
    context_t ctx={.p=p,.src=src,.data=data,.width=width,.height=height,.data_count=data_count,.steps=step_limit};
    for(size_t i=0;i<p->param_count;++i) ctx.params[i]=p->params[i].default_value;
    for(size_t i=0;i<parameter_count;++i) {
        if(!parameters[i].name) return JOLT_ERR_ARGUMENT;
        if(!isfinite(parameters[i].value)) return JOLT_ERR_NUMERIC;
        for(size_t j=0;j<i;++j) if(!strcmp(parameters[i].name,parameters[j].name)) return JOLT_ERR_ARGUMENT;
        size_t j=0; while(j<p->param_count && strcmp(parameters[i].name,p->params[j].name)) ++j;
        if(j==p->param_count || (p->params[j].integer && floor(parameters[i].value)!=parameters[i].value)) return JOLT_ERR_ARGUMENT;
        ctx.params[j]=fmin(p->params[j].maximum,fmax(p->params[j].minimum,parameters[i].value));
    }
    for(size_t i=0;i<values;++i) if(!isfinite(src[i])) return JOLT_ERR_NUMERIC;
    for(size_t i=0;i<data_count;++i) if(!isfinite(data[i])) return JOLT_ERR_NUMERIC;
    double passes=p->passes ? eval(&ctx,p->passes,0) : 1;
    if(ctx.status!=JOLT_OK) return ctx.status;
    if(passes<1 || passes>16 || floor(passes)!=passes) return JOLT_ERR_ARGUMENT;
    float *storage=tilly_container_alloc(2*bytes); if(!storage) return JOLT_ERR_MEMORY;
    float *output=storage;
    for(int pass=0;pass<(int)passes && ctx.status==JOLT_OK;++pass) {
        ctx.pass=pass;
        output=storage+(pass%2 ? values : 0);
        for(size_t y=0;y<height && ctx.status==JOLT_OK;++y) for(size_t x=0;x<width && ctx.status==JOLT_OK;++x) {
            for(size_t c=0;c<4 && ctx.status==JOLT_OK;++c) {
                double args[]={(double)x,(double)y,(double)c};
                double value=invoke(&ctx,p->entry,args,0);
                /* Conversion is checked before publishing any output. */
                if(fabs(value)>3.4028234663852886e38) { ctx.status=JOLT_ERR_NUMERIC; break; }
                output[4*(y*width+x)+c]=(float)value;
            }
        }
        ctx.src=output;
    }
    if(ctx.status==JOLT_OK) memcpy(dst,output,bytes);
    tilly_container_free(storage); return ctx.status;
}
