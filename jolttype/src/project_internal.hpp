#ifndef JOLTTYPE_PROJECT_INTERNAL_HPP
#define JOLTTYPE_PROJECT_INTERNAL_HPP
#include "jolttype/project.hpp"
#include <cstdio>
#include <stdexcept>
#include <charconv>
#include <cmath>
#include <cstring>
namespace jolttype::detail {
inline void fail(Diagnostic &e,const char *s) {Diagnostic next;std::snprintf(next.message,sizeof(next.message),"%s",s);e=next;}
inline bool utf8(std::string_view s) {
    for(size_t i=0;i<s.size();) {
        unsigned c=static_cast<unsigned char>(s[i++]),count=0;uint32_t cp=c,min=0;
        if(c<128){if(c<32 && c!='\n' && c!='\r' && c!='\t')return false;continue;}
        if(c>=0xc2 && c<=0xdf){count=1;cp=c&31;min=128;}
        else if(c>=0xe0 && c<=0xef){count=2;cp=c&15;min=2048;}
        else if(c>=0xf0 && c<=0xf4){count=3;cp=c&7;min=65536;}else return false;
        for(unsigned j=0;j<count;++j){if(i==s.size())return false;unsigned b=static_cast<unsigned char>(s[i++]);if((b&0xc0)!=0x80)return false;cp=(cp<<6)|(b&63);}
        if(cp<min || cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff))return false;
    }return true;
}
inline void need(bool condition,const char *message) {if(!condition)throw std::runtime_error(message);}
inline bool id(std::string_view s) {
    if(s.empty() || s.size()>63)return false;
    for(char c:s)if(!(c>='a' && c<='z') && !(c>='A' && c<='Z') && !(c>='0' && c<='9') && c!='_' && c!='-' && c!='.')return false;
    return s!="." && s!="..";
}
inline bool path(std::string_view s) {
    if(s.empty() || s.size()>255 || s[0]=='/' || s.back()=='/')return false;
    for(char c:s)if(static_cast<unsigned char>(c)<32 || c=='\\' || c==':')return false;
    size_t start=0;
    while(start<s.size()) {size_t end=s.find('/',start);if(end==s.npos)end=s.size();auto part=s.substr(start,end-start);if(part.empty() || part=="." || part=="..")return false;start=end+1;}
    return true;
}
inline tilly::string number(double n) {char b[64];auto r=std::to_chars(b,b+64,n,std::chars_format::general,12);need(r.ec==std::errc(),"Invalid number.");return {b,r.ptr};}
struct Writer {
    tilly::string data;
    void u32(uint32_t n) {for(unsigned i=0;i<4;++i)data+=char((n>>(8*i))&255);}
    void real(double n) {uint64_t v;std::memcpy(&v,&n,8);for(unsigned i=0;i<8;++i)data+=char((v>>(8*i))&255);}
    void text(std::string_view s) {need(s.size()<=max_inventory_bytes,"Field exceeds its budget.");u32(uint32_t(s.size()));data.append(s.data(),s.size());}
};
struct Reader {
    std::string_view data;size_t at=0;
    std::string_view take(size_t n) {need(n<=data.size()-at,"Truncated binary input.");auto s=data.substr(at,n);at+=n;return s;}
    uint32_t u32() {auto s=take(4);uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(static_cast<unsigned char>(s[i]))<<(i*8);return n;}
    double real() {auto s=take(8);uint64_t n=0;for(unsigned i=0;i<8;++i)n|=uint64_t(static_cast<unsigned char>(s[i]))<<(i*8);double v;std::memcpy(&v,&n,8);need(std::isfinite(v),"Non-finite binary dimension.");return v;}
    tilly::string text(size_t limit) {size_t n=u32();need(n<=limit,"Binary field exceeds its budget.");return tilly::string(take(n));}
    void end() {need(at==data.size(),"Trailing binary data.");}
};
}
#endif
