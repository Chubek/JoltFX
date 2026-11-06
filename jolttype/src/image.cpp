#include "jolttype/jolttype.hpp"
#include <cstdio>
#define STBI_MALLOC(n) tilly_alloc(const_cast<tilly_allocator_t *>(tilly_default_allocator()),(n),alignof(max_align_t))
#define STBI_REALLOC(p,n) tilly_realloc(const_cast<tilly_allocator_t *>(tilly_default_allocator()),(p),(n))
#define STBI_FREE(p) tilly_free(const_cast<tilly_allocator_t *>(tilly_default_allocator()),(p))
#define STBI_NO_STDIO
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
namespace jolttype {
bool decode_image(Element &e,Diagnostic &error) noexcept {
    try {
        int w=0,h=0,channels=0;
        if(e.image.size()>16*1024*1024 || !stbi_info_from_memory(reinterpret_cast<const unsigned char *>(e.image.data()),int(e.image.size()),&w,&h,&channels) || w<1 || h<1 || w>4096 || h>4096 || size_t(w)*size_t(h)>4*1024*1024) {
            error={};std::snprintf(error.message,sizeof(error.message),"Expected PNG/JPEG, at most 4096 per dimension and 4 megapixels.");return false;
        }
        struct Pixels {unsigned char *p;~Pixels(){stbi_image_free(p);}} pixels{stbi_load_from_memory(reinterpret_cast<const unsigned char *>(e.image.data()),int(e.image.size()),&w,&h,&channels,4)};
        if(!pixels.p){error={};std::snprintf(error.message,sizeof(error.message),"Image decoding failed.");return false;}
        tilly::vector<uint8_t> next(pixels.p,pixels.p+size_t(w)*size_t(h)*4);e.pixels.swap(next);e.image_width=unsigned(w);e.image_height=unsigned(h);error={};return true;
    }catch(...){error={};std::snprintf(error.message,sizeof(error.message),"Not enough memory to decode image.");return false;}
}
}
