#include "project_internal.hpp"
#include <mutex>
#include <cctype>
#ifdef JFX_JOLTTYPE_MODULES
#include "binaryen-c.h"
#include "jfx/script_runtime.h"
#endif

namespace jolttype {
using namespace detail;
#ifdef JFX_JOLTTYPE_MODULES
namespace {
std::mutex compiler_mutex;
bool letter(char c){return (c>='a' && c<='z') || (c>='A' && c<='Z') || c=='_';}
bool digit(char c){return c>='0' && c<='9';}
struct Expression {
    std::string_view s;size_t at=0,nodes=0;BinaryenModuleRef module;
    const tilly::vector<tilly::string> &parameters;
    void ws(){while(at<s.size() && std::isspace(static_cast<unsigned char>(s[at])))++at;}
    bool eat(char c){ws();if(at<s.size() && s[at]==c){++at;return true;}return false;}
    BinaryenExpressionRef expr(unsigned depth=0){
        need(depth<64 && ++nodes<4096,"Module expression budget exceeded.");auto left=term(depth+1);
        while(true){if(eat('+'))left=BinaryenBinary(module,BinaryenAddFloat64(),left,term(depth+1));else if(eat('-'))left=BinaryenBinary(module,BinaryenSubFloat64(),left,term(depth+1));else break;need(++nodes<4096,"Module expression budget exceeded.");}return left;
    }
    BinaryenExpressionRef term(unsigned depth){
        auto left=atom(depth+1);while(true){if(eat('*'))left=BinaryenBinary(module,BinaryenMulFloat64(),left,atom(depth+1));else if(eat('/'))left=BinaryenBinary(module,BinaryenDivFloat64(),left,atom(depth+1));else break;need(++nodes<4096,"Module expression budget exceeded.");}return left;
    }
    BinaryenExpressionRef atom(unsigned depth){
        need(depth<64 && ++nodes<4096,"Module expression budget exceeded.");
        if(eat('-'))return BinaryenUnary(module,BinaryenNegFloat64(),atom(depth+1));
        if(eat('+'))return atom(depth+1);
        if(eat('(')){auto e=expr(depth+1);need(eat(')'),"Missing ')' in module expression.");return e;}
        ws();need(at<s.size(),"Missing module expression.");
        if(digit(s[at]) || s[at]=='.'){
            double n;auto r=std::from_chars(s.data()+at,s.data()+s.size(),n,std::chars_format::general);
            need(r.ec==std::errc() && std::isfinite(n),"Invalid module number.");at=size_t(r.ptr-s.data());return BinaryenConst(module,BinaryenLiteralFloat64(n));
        }
        size_t start=at;while(at<s.size() && (letter(s[at]) || digit(s[at])))++at;need(at>start,"Unexpected expression token.");auto name=s.substr(start,at-start);
        if(eat('(')){
            auto first=expr(depth+1);
            if(name=="min" || name=="max"){need(eat(','),"min/max require two arguments.");auto second=expr(depth+1);need(eat(')'),"Missing ')'.");return BinaryenBinary(module,name=="min"?BinaryenMinFloat64():BinaryenMaxFloat64(),first,second);}
            need(name=="sqrt" || name=="abs" || name=="floor" || name=="ceil","Unknown module function.");need(eat(')'),"Missing ')'.");
            return BinaryenUnary(module,name=="sqrt"?BinaryenSqrtFloat64():name=="abs"?BinaryenAbsFloat64():name=="floor"?BinaryenFloorFloat64():BinaryenCeilFloat64(),first);
        }
        for(size_t i=0;i<parameters.size();++i)if(parameters[i]==name)return BinaryenLocalGet(module,BinaryenIndex(i),BinaryenTypeFloat64());
        throw std::runtime_error("Unknown module parameter.");
    }
};
struct Source {
    std::string_view s;size_t at=0;
    void skip(){while(at<s.size()){if(std::isspace(static_cast<unsigned char>(s[at])))++at;else if(s[at]=='%'){while(at<s.size() && s[at]!='\n')++at;}else break;}}
    std::string_view command(){skip();need(at<s.size() && s[at++]=='\\',"Expected a module command.");size_t start=at;while(at<s.size() && letter(s[at]))++at;return s.substr(start,at-start);}
    tilly::string argument(){skip();need(at<s.size() && s[at++]=='{',"Expected a module argument.");size_t start=at;while(at<s.size() && s[at]!='}'){need(s[at]!='{' && s[at]!='\\',"Nested module arguments are unsupported.");++at;}need(at<s.size(),"Unclosed module argument.");return tilly::string(s.substr(start,at++-start));}
};
}
#endif
bool modules_available() noexcept {
#ifdef JFX_JOLTTYPE_MODULES
    return true;
#else
    return false;
#endif
}
bool compile_module(std::string_view source,tilly::string &out,Diagnostic &error) noexcept {
#ifdef JFX_JOLTTYPE_MODULES
    try {
        need(!source.empty() && source.size()<=max_source_bytes && source.find('\0')==source.npos,"Invalid module source size.");
        std::lock_guard<std::mutex> guard(compiler_mutex);
        struct Owner {BinaryenModuleRef p;~Owner(){BinaryenModuleDispose(p);}} module{BinaryenModuleCreate()};need(module.p!=nullptr,"Binaryen allocation failed.");
        Source s{source};need(s.command()=="module","Module source starts with \\module{name}.");need(id(s.argument()),"Invalid module name.");
        tilly::vector<tilly::string> exports;
        while(true){s.skip();if(s.at==source.size())break;need(s.command()=="export","Expected \\export{name}{parameters}{expression}.");
            auto name=s.argument(),args=s.argument(),expression=s.argument();need(id(name) && exports.size()<64,"Invalid export name or too many exports.");
            for(const auto &old:exports)need(old!=name,"Duplicate module export.");
            exports.push_back(name);
            tilly::vector<tilly::string> parameters;size_t at=0;
            while(at<args.size()){
                size_t end=args.find(',',at);if(end==args.npos)end=args.size();std::string_view word(args.data()+at,end-at);
                while(!word.empty() && std::isspace(static_cast<unsigned char>(word.front())))word.remove_prefix(1);
                while(!word.empty() && std::isspace(static_cast<unsigned char>(word.back())))word.remove_suffix(1);
                need(!word.empty() && letter(word[0]) && parameters.size()<16,"Invalid module parameter list.");for(char c:word)need(letter(c) || digit(c),"Invalid parameter name.");
                for(const auto &old:parameters)need(old!=word,"Duplicate module parameter.");
                parameters.emplace_back(word);at=end+1;need(end==args.size() || at<args.size(),"Trailing parameter comma.");
            }
            Expression parser{expression,0,0,module.p,parameters};auto body=parser.expr();parser.ws();need(parser.at==expression.size(),"Trailing module expression data.");
            BinaryenType types[16];for(size_t i=0;i<parameters.size();++i)types[i]=BinaryenTypeFloat64();
            auto signature=parameters.empty()?BinaryenTypeNone():BinaryenTypeCreate(types,BinaryenIndex(parameters.size()));
            BinaryenAddFunction(module.p,name.c_str(),signature,BinaryenTypeFloat64(),nullptr,0,body);
            BinaryenAddFunctionExport(module.p,name.c_str(),name.c_str());
        }
        need(!exports.empty() && BinaryenModuleValidate(module.p),"Invalid or empty Binaryen module.");
        tilly::string bytes(1024*1024,'\0');size_t n=BinaryenModuleWrite(module.p,bytes.data(),bytes.size());need(n>0 && n<bytes.size(),"Compiled module exceeds 1 MiB.");bytes.resize(n);out.swap(bytes);error={};return true;
    }catch(const std::exception &e){fail(error,e.what());return false;}
#else
    (void)source;(void)out;fail(error,"Modules require JFX_JOLTTYPE_MODULES.");return false;
#endif
}
Module::~Module(){
#ifdef JFX_JOLTTYPE_MODULES
    jfx_script_runtime_destroy(static_cast<jfx_script_runtime_t *>(runtime_));
#endif
}
bool Module::load(std::string_view wasm,Diagnostic &error) noexcept {
#ifdef JFX_JOLTTYPE_MODULES
    if(wasm.empty() || wasm.size()>1024*1024){fail(error,"WASM module exceeds 1 MiB or is empty.");return false;}
    jfx_script_desc_t desc{};desc.size=sizeof(desc);desc.config.memory_limit=16*1024*1024;desc.config.instruction_limit=100000;
    jfx_script_runtime_t *next=nullptr;
    if(jfx_script_runtime_create(JFX_SCRIPT_WASM,&desc,&next)!=JFX_SCRIPT_OK){fail(error,"Cannot create WAMR module runtime.");return false;}
    if(jfx_script_runtime_load(next,wasm.data(),wasm.size(),"typelet.wasm")!=JFX_SCRIPT_OK){fail(error,jfx_script_runtime_last_error(next));jfx_script_runtime_destroy(next);return false;}
    jfx_script_runtime_destroy(static_cast<jfx_script_runtime_t *>(runtime_));runtime_=next;error={};return true;
#else
    (void)wasm;fail(error,"Modules require JFX_JOLTTYPE_MODULES.");return false;
#endif
}
bool Module::call(const char *name,const double *args,size_t count,double &result,Diagnostic &error) noexcept {
#ifdef JFX_JOLTTYPE_MODULES
    if(!runtime_ || !name || count>16 || (count && !args)){fail(error,"Invalid module invocation.");return false;}
    jfx_value_t values[16]{};for(size_t i=0;i<count;++i){if(!std::isfinite(args[i])){fail(error,"Module inputs must be finite.");return false;}values[i].type=JFX_TYPE_FLOAT;values[i].f=args[i];}
    auto *runtime=static_cast<jfx_script_runtime_t *>(runtime_);jfx_value_t out{};
    if(jfx_script_runtime_call(runtime,name,values,count,&out)!=JFX_SCRIPT_OK){fail(error,jfx_script_runtime_last_error(runtime));return false;}
    if(out.type!=JFX_TYPE_FLOAT || !std::isfinite(out.f)){fail(error,"Module must return a finite f64 value.");return false;}
    result=out.f;error={};return true;
#else
    (void)name;(void)args;(void)count;(void)result;fail(error,"Modules require JFX_JOLTTYPE_MODULES.");return false;
#endif
}
}
