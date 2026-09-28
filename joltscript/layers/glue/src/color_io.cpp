#include "joltscript/color_io.h"
#include <cstdio>
#if defined(JFX_HAVE_OCIO)
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif
extern "C" int jolt_color_io_available(void) {
#if defined(JFX_HAVE_OCIO)
    return 1;
#else
    return 0;
#endif
}
extern "C" size_t jolt_color_io_format_count(void) {
#if defined(JFX_HAVE_OCIO)
    return static_cast<size_t>(OCIO::FileTransform::GetNumFormats());
#else
    return 0;
#endif
}
extern "C" const char *jolt_color_io_format(size_t i) {
#if defined(JFX_HAVE_OCIO)
    return i < jolt_color_io_format_count() ? OCIO::FileTransform::GetFormatNameByIndex(static_cast<int>(i)) : nullptr;
#else
    (void)i; return nullptr;
#endif
}
extern "C" int jolt_color_io_bake(const char *path, unsigned edge, float *rgb, char *error, size_t cap) {
    if (!path || !rgb || edge < 2 || edge > 256) return -1;
#if defined(JFX_HAVE_OCIO)
    try {
        auto transform = OCIO::FileTransform::Create();
        transform->setSrc(path);
        auto processor = OCIO::Config::CreateRaw()->getProcessor(transform)->getDefaultCPUProcessor();
        for (unsigned b=0;b<edge;++b) for (unsigned g=0;g<edge;++g) for (unsigned r=0;r<edge;++r) {
            float *v=rgb+(((size_t)b*edge+g)*edge+r)*3;
            v[0]=(float)r/(float)(edge-1); v[1]=(float)g/(float)(edge-1); v[2]=(float)b/(float)(edge-1);
            processor->applyRGB(v);
        }
        return 0;
    } catch (const std::exception &e) {
        if (error && cap) std::snprintf(error,cap,"%s",e.what());
        return -1;
    }
#else
    if (error && cap) std::snprintf(error,cap,"This build has no OpenColorIO LUT reader");
    return -1;
#endif
}
