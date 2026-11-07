#include "project_internal.hpp"
#include <cstdlib>
#include <cstdarg>
#include <cctype>
#include <cassert>
#include <algorithm>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <ctype.h>
#include <stdio.h>

// Upstream KSON assumes allocation succeeds. A bounded Tilly arena with throwing
// allocation makes every exit reclaim all parser storage, including OOM exits.
namespace {
struct KsonArena {
    tilly::vector<max_align_t> storage;
    size_t used=0;
    KsonArena():storage(8*1024*1024/sizeof(max_align_t)){}
    void *allocate(size_t n) {
        constexpr size_t alignment=alignof(max_align_t);
        size_t start=(used+alignment-1)&~(alignment-1);
        size_t size=storage.size()*sizeof(max_align_t);
        if(n>size || start>size-n || alignment>size-start-n)throw std::bad_alloc();
        auto *p=reinterpret_cast<unsigned char *>(storage.data())+start;std::memcpy(p,&n,sizeof(n));used=start+alignment+n;return p+alignment;
    }
};
thread_local KsonArena *kson_arena=nullptr;
void *kson_allocate(size_t n) {return kson_arena->allocate(n);}
void *kson_resize(void *p,size_t n) {
    void *q=kson_allocate(n);if(p){size_t old=0;std::memcpy(&old,static_cast<char *>(p)-alignof(max_align_t),sizeof(old));std::memcpy(q,p,std::min(old,n));}return q;
}
void *kson_zero(size_t count,size_t size) {if(size && count>SIZE_MAX/size)throw std::bad_alloc();void *p=kson_allocate(count*size);std::memset(p,0,count*size);return p;}
void kson_release(void *) {}
}
#define malloc kson_allocate
#define realloc kson_resize
#define calloc kson_zero
#define free kson_release
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wshadow"
#endif
#include "kson.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#undef malloc
#undef realloc
#undef calloc
#undef free

namespace jolttype {
namespace {
using namespace detail;
// KSON is permissive and stops at a completed root. Validate bounded JSON syntax
// first, including escaped endings, before letting KSON deserialize the tree.
struct JsonSyntax {
    std::string_view s;size_t at=0,nodes=0;
    void ws(){while(at<s.size() && (s[at]==' ' || s[at]=='\t' || s[at]=='\r' || s[at]=='\n'))++at;}
    char peek(){ws();return at<s.size()?s[at]:0;}
    void take(char c){need(peek()==c,"Invalid MANIFEST syntax.");++at;}
    void string(){
        take('"');while(at<s.size()){
            auto c=static_cast<unsigned char>(s[at++]);if(c=='"')return;
            need(c>=32,"Control character in MANIFEST string.");
            if(c=='\\'){
                need(at<s.size(),"Unclosed MANIFEST escape.");char e=s[at++];
                if(e=='u'){for(int i=0;i<4;++i){need(at<s.size() && std::isxdigit(static_cast<unsigned char>(s[at])),"Invalid Unicode escape.");++at;}}
                else need(std::strchr("\"\\/bfnrt",e)!=nullptr,"Invalid MANIFEST escape.");
            }
        }throw std::runtime_error("Unclosed MANIFEST string.");
    }
    void value(unsigned depth){
        need(depth<=24 && ++nodes<=4096,"MANIFEST nesting/node budget exceeded.");
        char c=peek();
        if(c=='"'){string();return;}
        if(c=='{' || c=='['){
            ++at;char end=c=='{'?'}':']';
            if(peek()==end){++at;return;}
            while(true){if(c=='{'){string();take(':');}value(depth+1);if(peek()==end){++at;return;}take(',');}
        }
        size_t start=at;while(at<s.size() && s[at]!=',' && s[at]!=']' && s[at]!='}' && s[at]!=' ' && s[at]!='\n' && s[at]!='\r' && s[at]!='\t')++at;
        auto token=s.substr(start,at-start);
        need(token=="1","MANIFEST values must be strings, arrays, objects or schema version 1.");
    }
    void validate(){need(!s.empty() && s.size()<=max_source_bytes && s.find('\0')==s.npos,"Invalid MANIFEST size or NUL.");value(0);ws();need(at==s.size(),"Trailing MANIFEST data.");}
};
unsigned hex(char c){return c<='9'?unsigned(c-'0'):unsigned((c|32)-'a'+10);}
tilly::string string(const kson_node_t *node,size_t limit=1024) {
    need(node && node->type==KSON_TYPE_DBL_QUOTE,"Expected a quoted MANIFEST string.");
    std::string_view s(node->v.str);need(s.size()<=limit*6,"MANIFEST string is too long.");tilly::string out;
    for(size_t i=0;i<s.size();++i){
        char c=s[i];if(c!='\\'){out+=c;continue;}c=s[++i];
        if(c=='u'){
            uint32_t cp=0;for(int j=0;j<4;++j)cp=(cp<<4)|hex(s[++i]);
            if(cp>=0xd800 && cp<=0xdbff){need(i+6<s.size() && s[i+1]=='\\' && s[i+2]=='u',"Unpaired Unicode surrogate.");i+=2;uint32_t low=0;for(int j=0;j<4;++j)low=(low<<4)|hex(s[++i]);need(low>=0xdc00 && low<=0xdfff,"Unpaired Unicode surrogate.");cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);}
            else need(cp<0xdc00 || cp>0xdfff,"Unpaired Unicode surrogate.");
            need(cp>=32,"Control character in MANIFEST string.");
            if(cp<0x80)out+=char(cp);else if(cp<0x800){out+=char(0xc0|(cp>>6));out+=char(0x80|(cp&63));}
            else if(cp<0x10000){out+=char(0xe0|(cp>>12));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
            else{out+=char(0xf0|(cp>>18));out+=char(0x80|((cp>>12)&63));out+=char(0x80|((cp>>6)&63));out+=char(0x80|(cp&63));}
        } else if(c=='n')out+='\n';else if(c=='r')out+='\r';else if(c=='t')out+='\t';else if(c=='b' || c=='f')throw std::runtime_error("Unsupported control character.");else out+=c;
    }
    need(out.size()<=limit && utf8(out),"MANIFEST string is too long or not UTF-8.");return out;
}
void object(const kson_node_t *n,std::initializer_list<const char *> allowed) {
    need(n && n->type==KSON_TYPE_BRACE,"Expected a MANIFEST object.");
    for(size_t i=0;i<n->n;++i){auto *c=n->v.child[i];need(c->key!=nullptr,"Missing MANIFEST key.");bool known=false;for(auto a:allowed)if(!std::strcmp(c->key,a))known=true;need(known,"Unknown MANIFEST key.");
        for(size_t j=0;j<i;++j)need(std::strcmp(c->key,n->v.child[j]->key)!=0,"Duplicate MANIFEST key.");}
}
const kson_node_t *array(const kson_node_t *n,size_t limit){need(n && n->type==KSON_TYPE_BRACKET && n->n<=limit,"Expected a bounded MANIFEST array.");return n;}
tilly::vector<tilly::string> strings(const kson_node_t *n,size_t limit=32) {
    tilly::vector<tilly::string> out;if(!n)return out;array(n,limit);for(size_t i=0;i<n->n;++i)out.push_back(string(n->v.child[i],63));return out;
}
const char *kind_name(AssetKind kind){static const char *names[]={"image","text","code","source","compiled","module","other","font","pagelet","kliche"};return names[unsigned(kind)];}
AssetKind kind(std::string_view name){for(unsigned i=0;i<=unsigned(AssetKind::Kliche);++i)if(name==kind_name(AssetKind(i)))return AssetKind(i);throw std::runtime_error("Unknown asset kind.");}
tilly::string quote(std::string_view s){tilly::string out="\"";for(char c:s){if(c=='"' || c=='\\'){out+='\\';out+=c;}else if(c=='\n')out+="\\n";else if(c=='\r')out+="\\r";else if(c=='\t')out+="\\t";else {need(static_cast<unsigned char>(c)>=32,"Invalid manifest text.");out+=c;}}return out+'"';}
tilly::string list(const tilly::vector<tilly::string>& values){tilly::string out="[";for(const auto &v:values){if(out.size()>1)out+=',';out+=quote(v);}return out+"]";}
}
bool parse_manifest(std::string_view source,Manifest &out,tilly::vector<Asset> &assets,Diagnostic &error) noexcept {
    try {
        JsonSyntax{source}.validate();KsonArena arena;
        struct Scope {KsonArena *old;Scope(KsonArena &a):old(kson_arena){kson_arena=&a;}~Scope(){kson_arena=old;}} scope(arena);
        tilly::string text(source);auto *tree=kson_parse(text.c_str());need(tree && tree->root,"KSON could not parse MANIFEST.");auto *root=tree->root;
        object(root,{"schema","name","version","author","description","kind","entry","dependencies","compile","interfaces"});
        auto *schema=kson_by_key(root,"schema");need(schema && schema->type==KSON_TYPE_NO_QUOTE && !std::strcmp(schema->v.str,"1"),"Unsupported MANIFEST schema.");
        Manifest m;m.name=string(kson_by_key(root,"name"),128);m.version=string(kson_by_key(root,"version"),63);
        m.entry=string(kson_by_key(root,"entry"),63);m.kind=string(kson_by_key(root,"kind"),16);
        need(!m.name.empty() && id(m.version) && id(m.entry),"Invalid manifest identity.");
        need(m.kind=="document" || m.kind=="module" || m.kind=="mixed","Unknown typelet kind.");
        if(auto *n=kson_by_key(root,"author"))m.author=string(n,256);
        if(auto *n=kson_by_key(root,"description"))m.description=string(n,4096);
        tilly::vector<Asset> next;auto *deps=array(kson_by_key(root,"dependencies"),max_assets);
        for(size_t i=0;i<deps->n;++i){auto *n=deps->v.child[i];object(n,{"id","path","kind","tags","requires"});Asset a;
            a.id=string(kson_by_key(n,"id"),63);a.path=string(kson_by_key(n,"path"),255);a.kind=kind(string(kson_by_key(n,"kind"),16));
            a.tags=strings(kson_by_key(n,"tags"));a.dependencies=strings(kson_by_key(n,"requires"));next.push_back(std::move(a));}
        Inventory check;need(check.replace(next,error),error.message);need(check.find(m.entry)!=nullptr,"Entry dependency is missing.");
        if(auto *steps=kson_by_key(root,"compile")){array(steps,32);for(size_t i=0;i<steps->n;++i){auto *n=steps->v.child[i];object(n,{"input","output","target"});BuildStep step;
            step.input=string(kson_by_key(n,"input"),63);step.output=string(kson_by_key(n,"output"),63);step.target=string(kson_by_key(n,"target"),16);
            need(id(step.input) && id(step.output) && step.input!=step.output,"Invalid compile step IDs.");
            need(step.target=="pdf" || step.target=="pdf-podofo" || step.target=="svg" || step.target=="ps" || step.target=="dvi" || step.target=="jtbc" || step.target=="wasm","Unknown compile target.");m.build.push_back(std::move(step));}}
        if(auto *interfaces=kson_by_key(root,"interfaces")){array(interfaces,64);for(size_t i=0;i<interfaces->n;++i){auto *n=interfaces->v.child[i];object(n,{"id","module","export","label","parameters"});Interface ui;
            ui.id=string(kson_by_key(n,"id"),63);ui.module=string(kson_by_key(n,"module"),63);ui.entry=string(kson_by_key(n,"export"),63);ui.label=string(kson_by_key(n,"label"),128);ui.parameters=strings(kson_by_key(n,"parameters"),16);
            need(id(ui.id) && id(ui.module) && id(ui.entry),"Invalid module interface.");for(const auto &other:m.interfaces)need(other.id!=ui.id,"Duplicate interface ID.");
            for(const auto &param:ui.parameters)need(id(param),"Invalid parameter name.");
            m.interfaces.push_back(std::move(ui));}}
        out=std::move(m);assets=std::move(next);error={};return true;
    }catch(const std::exception &e){detail::fail(error,e.what());return false;}
}
bool manifest_text(const Typelet &p,tilly::string &out,Diagnostic &error) noexcept {
    try {
        const auto &m=p.manifest;tilly::string text="{\n\"schema\":1,\n\"name\":"+quote(m.name)+",\n\"version\":"+quote(m.version)+",\n\"author\":"+quote(m.author)+",\n\"description\":"+quote(m.description)+",\n\"kind\":"+quote(m.kind)+",\n\"entry\":"+quote(m.entry)+",\n\"dependencies\":[\n";
        bool comma=false;for(const auto &a:p.inventory.assets()) {if(comma)text+=",\n";comma=true;need(unsigned(a.kind)<=unsigned(AssetKind::Kliche),"Invalid asset kind.");text+="{\"id\":"+quote(a.id)+",\"path\":"+quote(a.path)+",\"kind\":"+quote(kind_name(a.kind))+",\"tags\":"+list(a.tags)+",\"requires\":"+list(a.dependencies)+"}";}
        text+="\n],\n\"compile\":[";comma=false;for(const auto &s:m.build){if(comma)text+=',';comma=true;text+="{\"input\":"+quote(s.input)+",\"output\":"+quote(s.output)+",\"target\":"+quote(s.target)+"}";}
        text+="],\n\"interfaces\":[";comma=false;for(const auto &i:m.interfaces){if(comma)text+=',';comma=true;text+="{\"id\":"+quote(i.id)+",\"module\":"+quote(i.module)+",\"export\":"+quote(i.entry)+",\"label\":"+quote(i.label)+",\"parameters\":"+list(i.parameters)+"}";}
        text+="]\n}\n";Manifest validated;tilly::vector<Asset> deps;if(!parse_manifest(text,validated,deps,error))return false;out.swap(text);return true;
    }catch(const std::exception &e){detail::fail(error,e.what());return false;}
}
}
