#include "jfx/jfx_vst3.h"
#include "tilly/allocator.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <new>

#ifdef JFX_AUDIO_VST3
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/base/ipluginbase.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstattributes.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivsthostapplication.h"
#include "pluginterfaces/vst/ivstmessage.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/vstspeaker.h"
#include <filesystem>
#include <memory>
#include <string>
#include <chrono>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#else
#include <dlfcn.h>
#include <poll.h>
#endif

namespace Steinberg
{
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
namespace Linux { DEF_CLASS_IID(IRunLoop) }
namespace Vst
{
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IComponentHandler)
DEF_CLASS_IID(IComponentHandler2)
DEF_CLASS_IID(IConnectionPoint)
DEF_CLASS_IID(IParameterChanges)
DEF_CLASS_IID(IParamValueQueue)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
DEF_CLASS_IID(IEventList)
} // namespace Vst
} // namespace Steinberg
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace
{
void *alloc(size_t n)
{
    return tilly_alloc((tilly_allocator_t *) tilly_default_allocator(), n,
                       alignof(std::max_align_t));
}
void drop(void *p)
{
    tilly_free((tilly_allocator_t *) tilly_default_allocator(), p);
}
template <class T> T *make()
{
    void *p = alloc(sizeof(T));
    return p ? new (p) T() : nullptr;
}
template <class T> void dispose(T *p)
{
    if (p) {
        p->~T();
        drop(p);
    }
}
bool equal(const TUID a, const FUID &b)
{
    return !std::memcmp(a, b.toTUID(), 16);
}
template <class T> bool query(FUnknown *from, T **out)
{
    return from &&
           from->queryInterface(T::iid.toTUID(), reinterpret_cast<void **>(out)) == kResultOk &&
           *out;
}
void utf8(const TChar *in, char *out, size_t cap)
{
    size_t n = 0;
    for (size_t i = 0; i < 128 && in[i]; ++i) {
        uint32_t c = (uint16_t) in[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < 128 && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF)
            c = 0x10000 + ((c - 0xD800) << 10) + ((uint16_t) in[++i] - 0xDC00);
        unsigned bytes = c < 128 ? 1 : c < 2048 ? 2 : c < 65536 ? 3 : 4;
        if (n + bytes >= cap)
            break;
        if (bytes == 1)
            out[n++] = (char) c;
        else {
            out[n++] = (char) ((bytes == 2   ? 0xC0
                                : bytes == 3 ? 0xE0
                                             : 0xF0) |
                               (c >> (6 * (bytes - 1))));
            for (unsigned b = bytes - 1; b > 0; --b)
                out[n++] = (char) (0x80 | ((c >> (6 * (b - 1))) & 63));
        }
    }
    out[n] = 0;
}
class Attributes final : public IAttributeList
{
    struct Entry {
        char key[64]{};
        int type = 0;
        int64 integer = 0;
        double floating = 0;
        void *data = nullptr;
        uint32 bytes = 0;
    } entries[32];
    uint32 refs = 1;
    Entry *entry(AttrID id, bool create)
    {
        if (!id || !*id || std::strlen(id) >= 64)
            return nullptr;
        Entry *empty = nullptr;
        for (auto &e : entries) {
            if (!std::strcmp(e.key, id))
                return &e;
            if (!e.key[0] && !empty)
                empty = &e;
        }
        if (create && empty) {
            std::strcpy(empty->key, id);
            return empty;
        }
        return nullptr;
    }
    tresult blob(AttrID id, const void *data, uint32 bytes, int type)
    {
        if ((!data && bytes) || bytes > 1024 * 1024)
            return kInvalidArgument;
        auto *e = entry(id, true);
        if (!e)
            return kOutOfMemory;
        void *copy = bytes ? alloc(bytes) : nullptr;
        if (bytes && !copy)
            return kOutOfMemory;
        if (bytes)
            std::memcpy(copy, data, bytes);
        drop(e->data);
        e->data = copy;
        e->bytes = bytes;
        e->type = type;
        return kResultOk;
    }

  public:
    ~Attributes()
    {
        for (auto &e : entries)
            drop(e.data);
    }
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (!equal(id, FUnknown::iid) && !equal(id, IAttributeList::iid))
            return kNoInterface;
        *out = this;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override
    {
        return ++refs;
    }
    uint32 PLUGIN_API release() override
    {
        uint32 n = --refs;
        if (!n)
            dispose(this);
        return n;
    }
    tresult PLUGIN_API setInt(AttrID id, int64 value) override
    {
        auto *e = entry(id, true);
        if (!e)
            return kInvalidArgument;
        e->integer = value;
        e->type = 1;
        return kResultOk;
    }
    tresult PLUGIN_API getInt(AttrID id, int64 &value) override
    {
        auto *e = entry(id, false);
        if (!e || e->type != 1)
            return kResultFalse;
        value = e->integer;
        return kResultOk;
    }
    tresult PLUGIN_API setFloat(AttrID id, double value) override
    {
        auto *e = entry(id, true);
        if (!e)
            return kInvalidArgument;
        e->floating = value;
        e->type = 2;
        return kResultOk;
    }
    tresult PLUGIN_API getFloat(AttrID id, double &value) override
    {
        auto *e = entry(id, false);
        if (!e || e->type != 2)
            return kResultFalse;
        value = e->floating;
        return kResultOk;
    }
    tresult PLUGIN_API setString(AttrID id, const TChar *text) override
    {
        if (!text)
            return kInvalidArgument;
        uint32 n = 0;
        while (n < 65536 && text[n])
            ++n;
        if (n == 65536)
            return kInvalidArgument;
        return blob(id, text, (n + 1) * (uint32) sizeof(TChar), 3);
    }
    tresult PLUGIN_API getString(AttrID id, TChar *out, uint32 bytes) override
    {
        auto *e = entry(id, false);
        if (!e || e->type != 3 || !out || bytes < sizeof(TChar))
            return kResultFalse;
        uint32 n = std::min(e->bytes, bytes);
        n -= n % (uint32) sizeof(TChar);
        std::memcpy(out, e->data, n);
        out[n / sizeof(TChar) - 1] = 0;
        return kResultOk;
    }
    tresult PLUGIN_API setBinary(AttrID id, const void *data, uint32 bytes) override
    {
        return blob(id, data, bytes, 4);
    }
    tresult PLUGIN_API getBinary(AttrID id, const void *&data, uint32 &bytes) override
    {
        auto *e = entry(id, false);
        if (!e || e->type != 4)
            return kResultFalse;
        data = e->data;
        bytes = e->bytes;
        return kResultOk;
    }
};
class Message final : public IMessage
{
    uint32 refs = 1;
    char id[256]{};

  public:
    Attributes *attributes = make<Attributes>();
    ~Message()
    {
        if (attributes)
            attributes->release();
    }
    tresult PLUGIN_API queryInterface(const TUID uid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (!equal(uid, FUnknown::iid) && !equal(uid, IMessage::iid))
            return kNoInterface;
        *out = this;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override
    {
        return ++refs;
    }
    uint32 PLUGIN_API release() override
    {
        uint32 n = --refs;
        if (!n)
            dispose(this);
        return n;
    }
    FIDString PLUGIN_API getMessageID() override
    {
        return id;
    }
    void PLUGIN_API setMessageID(FIDString text) override
    {
        std::snprintf(id, sizeof(id), "%s", text ? text : "");
    }
    IAttributeList *PLUGIN_API getAttributes() override
    {
        return attributes;
    }
};
class Host final : public IHostApplication, public IComponentHandler
{
  public:
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (equal(id, FUnknown::iid) || equal(id, IHostApplication::iid))
            *out = static_cast<IHostApplication *>(this);
        else if (equal(id, IComponentHandler::iid))
            *out = static_cast<IComponentHandler *>(this);
        else
            return kNoInterface;
        addRef();
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }
    tresult PLUGIN_API getName(String128 name) override
    {
        const char *text = "JoltFX DAW";
        std::memset(name, 0, sizeof(String128));
        for (size_t i = 0; text[i]; ++i)
            name[i] = (TChar) text[i];
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(TUID cid, TUID iid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (equal(cid, IMessage::iid) && equal(iid, IMessage::iid)) {
            auto *message = make<Message>();
            if (!message)
                return kOutOfMemory;
            if (!message->attributes) {
                dispose(message);
                return kOutOfMemory;
            }
            *out = static_cast<IMessage *>(message);
            return kResultOk;
        }
        if (equal(cid, IAttributeList::iid) && equal(iid, IAttributeList::iid)) {
            auto *attributes = make<Attributes>();
            if (!attributes)
                return kOutOfMemory;
            *out = static_cast<IAttributeList *>(attributes);
            return kResultOk;
        }
        return kNoInterface;
    }
    tresult PLUGIN_API beginEdit(ParamID) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API performEdit(ParamID, ParamValue) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API endEdit(ParamID) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API restartComponent(int32) override
    {
        return kNotImplemented;
    }
};
Host host;

/* Module initialization is once per canonical path, even with many rack slots. */
struct Module {
    Module *next = nullptr;
    uint32_t refs = 1;
    char path[JFX_NODE_PATH_MAX]{};
    void *handle = nullptr;
    IPluginFactory *factory = nullptr;
    bool entered = false;
    void *symbol(const char *name)
    {
#if defined(_WIN32)
        return reinterpret_cast<void *>(GetProcAddress((HMODULE) handle, name));
#elif defined(__APPLE__)
        auto key = CFStringCreateWithCString(nullptr, name, kCFStringEncodingUTF8);
        auto p = CFBundleGetFunctionPointerForName((CFBundleRef) handle, key);
        CFRelease(key);
        return p;
#else
        return dlsym(handle, name);
#endif
    }
    ~Module()
    {
        if (factory)
            factory->release();
        if (!handle)
            return;
#if defined(_WIN32)
        if (entered) {
            auto exit = reinterpret_cast<bool (*)()>(symbol("ExitDll"));
            if (exit)
                exit();
        }
        FreeLibrary((HMODULE) handle);
#elif defined(__APPLE__)
        if (entered) {
            auto exit = reinterpret_cast<bool (*)()>(symbol("bundleExit"));
            if (exit)
                exit();
        }
        CFBundleUnloadExecutable((CFBundleRef) handle);
        CFRelease((CFBundleRef) handle);
#else
        if (entered) {
            auto exit = reinterpret_cast<bool (*)()>(symbol("ModuleExit"));
            if (exit)
                exit();
        }
        dlclose(handle);
#endif
    }
};
Module *modules = nullptr;
void release_module(Module *m)
{
    if (!m || --m->refs)
        return;
    Module **p = &modules;
    while (*p && *p != m)
        p = &(*p)->next;
    if (*p)
        *p = m->next;
    dispose(m);
}
Module *open_module(const char *path)
{
    namespace fs = std::filesystem;
    std::error_code ec;
    auto bundle = fs::canonical(fs::u8path(path), ec);
    if (ec)
        return nullptr;
    auto canonical = bundle.u8string();
    if (canonical.size() >= JFX_NODE_PATH_MAX)
        return nullptr;
    for (auto *m = modules; m; m = m->next)
        if (!std::strcmp(m->path, canonical.c_str())) {
            ++m->refs;
            return m;
        }
    auto *m = make<Module>();
    if (!m)
        return nullptr;
    std::unique_ptr<Module, void (*)(Module *)> guard(m, dispose<Module>);
    std::snprintf(m->path, sizeof(m->path), "%s", canonical.c_str());
#if defined(__APPLE__)
    auto url = CFURLCreateFromFileSystemRepresentation(nullptr, (const UInt8 *) canonical.c_str(),
                                                       (CFIndex) canonical.size(), true);
    m->handle = CFBundleCreate(nullptr, url);
    CFRelease(url);
    if (!m->handle || !CFBundleLoadExecutable((CFBundleRef) m->handle))
        return nullptr;
    auto entry = reinterpret_cast<bool (*)(CFBundleRef)>(m->symbol("bundleEntry"));
    m->entered = entry && entry((CFBundleRef) m->handle);
#else
    auto binary = bundle;
    if (fs::is_directory(bundle, ec)) {
#if defined(_WIN32)
#if defined(_M_ARM64)
        const char *arch = "arm64-win";
#elif defined(_WIN64)
        const char *arch = "x86_64-win";
#else
        const char *arch = "x86-win";
#endif
        const char *suffix = ".vst3";
#else
#if defined(__aarch64__)
        const char *arch = "aarch64-linux";
#elif defined(__x86_64__)
        const char *arch = "x86_64-linux";
#elif defined(__i386__)
        const char *arch = "i386-linux";
#else
        const char *arch = "unknown-linux";
#endif
        const char *suffix = ".so";
#endif
        binary = bundle / "Contents" / arch / (bundle.stem().u8string() + suffix);
        if (!fs::is_regular_file(binary, ec)) {
            binary.clear();
            for (fs::directory_iterator it(bundle / "Contents" / arch, ec), end; !ec && it != end;
                 it.increment(ec))
                if (it->path().extension() == suffix) {
                    binary = it->path();
                    break;
                }
        }
    }
    /* A bundle and a direct binary path can designate the same loaded module. */
    auto identity = fs::canonical(binary, ec).u8string();
    if (ec || identity.size() >= sizeof(m->path))
        return nullptr;
    for (auto *existing = modules; existing; existing = existing->next)
        if (!std::strcmp(existing->path, identity.c_str())) {
            ++existing->refs;
            return existing;
        }
    std::snprintf(m->path, sizeof(m->path), "%s", identity.c_str());
#if defined(_WIN32)
    m->handle = LoadLibraryW(binary.c_str());
    if (m->handle) {
        auto entry = reinterpret_cast<bool (*)()>(m->symbol("InitDll"));
        m->entered = entry && entry();
    }
#else
    m->handle = dlopen(binary.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (m->handle) {
        auto entry = reinterpret_cast<bool (*)(void *)>(m->symbol("ModuleEntry"));
        m->entered = entry && entry(m->handle);
    }
#endif
#endif
    if (!m->handle || !m->entered)
        return nullptr;
    auto factory =
        reinterpret_cast<IPluginFactory *(PLUGIN_API *) ()>(m->symbol("GetPluginFactory"));
    m->factory = factory ? factory() : nullptr;
    if (!m->factory)
        return nullptr;
    IPluginFactory3 *f3 = nullptr;
    if (query(m->factory, &f3)) {
        f3->setHostContext(static_cast<IHostApplication *>(&host));
        f3->release();
    }
    m->next = modules;
    modules = m;
    return guard.release();
}

class Queue final : public IParamValueQueue
{
  public:
    ParamID id = 0;
    double value = 0;
    struct Point { int32 offset; ParamValue value; } points[511];
    int32 extra=0;
    tresult PLUGIN_API queryInterface(const TUID uid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (!equal(uid, IParamValueQueue::iid) && !equal(uid, FUnknown::iid))
            return kNoInterface;
        *out = this;
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }
    ParamID PLUGIN_API getParameterId() override
    {
        return id;
    }
    int32 PLUGIN_API getPointCount() override
    {
        return 1+extra;
    }
    tresult PLUGIN_API getPoint(int32 index, int32 &offset, ParamValue &out) override
    {
        if (index<0 || index>extra)
            return kInvalidArgument;
        offset = index?points[index-1].offset:0;
        out = index?points[index-1].value:value;
        return kResultOk;
    }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32 &) override
    {
        return kNotImplemented;
    }
};
class Changes final : public IParameterChanges
{
  public:
    Queue queues[JFX_VST3_MAX_PARAM_QUEUES];
    int32 count = 0;
    tresult PLUGIN_API queryInterface(const TUID uid, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (!equal(uid, IParameterChanges::iid) && !equal(uid, FUnknown::iid))
            return kNoInterface;
        *out = this;
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return count;
    }
    IParamValueQueue *PLUGIN_API getParameterData(int32 i) override
    {
        return i >= 0 && i < count ? queues + i : nullptr;
    }
    IParamValueQueue *PLUGIN_API addParameterData(const ParamID &, int32 &) override
    {
        return nullptr;
    }
};
/* Bounded initialization state stream synchronizes separate controllers. */
class Stream final : public IBStream
{
  public:
    char *data = nullptr;
    int64 length = 0, pos = 0;
    bool failed = false;
    Stream()
    {
        data = (char *) alloc(1024 * 1024);
    }
    ~Stream()
    {
        drop(data);
    }
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = nullptr;
        if (!equal(id, IBStream::iid) && !equal(id, FUnknown::iid))
            return kNoInterface;
        *out = this;
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override
    {
        return 1;
    }
    uint32 PLUGIN_API release() override
    {
        return 1;
    }
    tresult PLUGIN_API read(void *out, int32 n, int32 *done) override
    {
        if (!data || !out || n < 0)
            return kInvalidArgument;
        int32 count = (int32) std::min<int64>(n, length - pos);
        std::memcpy(out, data + pos, (size_t) count);
        pos += count;
        if (done)
            *done = count;
        return count == n ? kResultOk : kResultFalse;
    }
    tresult PLUGIN_API write(void *in, int32 n, int32 *done) override
    {
        if (!data || !in || n < 0 || n > 1024 * 1024 - pos) {
            failed=true;
            return kInvalidArgument;
        }
        std::memcpy(data + pos, in, (size_t) n);
        pos += n;
        length = std::max(length, pos);
        if (done)
            *done = n;
        return kResultOk;
    }
    tresult PLUGIN_API seek(int64 n, int32 mode, int64 *result) override
    {
        int64 base = mode == kIBSeekSet   ? 0
                     : mode == kIBSeekCur ? pos
                     : mode == kIBSeekEnd ? length
                                          : -1;
        if (base < 0 || n < -base || n > length - base)
            return kInvalidArgument;
        pos = base + n;
        if (result)
            *result = pos;
        return kResultOk;
    }
    tresult PLUGIN_API tell(int64 *out) override
    {
        if (!out)
            return kInvalidArgument;
        *out = pos;
        return kResultOk;
    }
};
#include "vst3_ui.inc"
} // namespace
struct jfx_vst3_instance {
    Module *module = nullptr;
    IComponent *component = nullptr;
    IAudioProcessor *processor = nullptr;
    IEditController *controller = nullptr;
    IConnectionPoint *component_connection = nullptr, *controller_connection = nullptr;
    bool initialized = false, controller_initialized = false, active = false, processing = false,
         connected = false;
    uint32_t rate = 0, block = 0, latency = 0;
    double tempo = 120;
    float *buffers = nullptr;
    Changes changes;
    bool send_changes = true;
    bool instrument = false, midi = false;
    Events events;
    Handler handler;
    PlugFrame frame;
    IPlugView *view = nullptr;
    bool attached = false;
    ~jfx_vst3_instance()
    {
        jfx_vst3_editor_close(this);
        if (processing)
            processor->setProcessing(false);
        if (active)
            component->setActive(false);
        if (connected) {
            component_connection->disconnect(controller_connection);
            controller_connection->disconnect(component_connection);
        }
        if (component_connection)
            component_connection->release();
        if (controller_connection)
            controller_connection->release();
        if (controller) {
            controller->setComponentHandler(nullptr);
            if (controller_initialized)
                controller->terminate();
            controller->release();
        }
        if (processor)
            processor->release();
        if (component) {
            if (initialized)
                component->terminate();
            component->release();
        }
        drop(buffers);
        release_module(module);
    }
};
extern "C" bool jfx_vst3_available(void)
{
    return true;
}
extern "C" void jfx_vst3_destroy(jfx_vst3_instance_t *p)
{
    dispose(p);
}
extern "C" jfx_result_t jfx_vst3_classes(const char *path, jfx_vst3_class_t *out, size_t cap,
                                         size_t *count)
{
    if (!path || !*path || !count || (cap && !out))
        return JFX_ERROR_INVALID_ARGUMENT;
    try {
        auto *m = open_module(path);
        if (!m)
            return JFX_ERROR_NOT_FOUND;
        size_t n = 0;
        for (int32 i = 0; i < m->factory->countClasses(); ++i) {
            PClassInfo info{};
            if (m->factory->getClassInfo(i, &info) != kResultOk ||
                std::strncmp(info.category, kVstAudioEffectClass, sizeof(info.category)))
                continue;
            if (n < cap) {
                out[n] = {};
                out[n].size = sizeof(out[n]);
                FUID(info.cid).toString(out[n].cid);
                std::snprintf(out[n].name, sizeof(out[n].name), "%.*s", (int) sizeof(info.name),
                              info.name);
            }
            ++n;
        }
        release_module(m);
        *count = n;
        return n > cap && cap ? JFX_ERROR_OUT_OF_MEMORY : JFX_SUCCESS;
    } catch (...) {
        return JFX_ERROR_OUT_OF_MEMORY;
    }
}
extern "C" jfx_result_t jfx_vst3_create(const jfx_audio_insert_t *in, uint32_t rate, uint32_t block,
                                         jfx_vst3_instance_t **out)
{
    return jfx_vst3_create_with_state(in,nullptr,0,rate,block,out);
}
extern "C" jfx_result_t jfx_vst3_create_with_state(const jfx_audio_insert_t *in,const void *state,size_t bytes,uint32_t rate,uint32_t block,jfx_vst3_instance_t **out)
{
    if (!in || in->size < sizeof(*in) || !out || rate < 8000 || rate > 192000 || !block ||
        block > JFX_AUDIO_MAX_BLOCK_FRAMES || !std::memchr(in->path, 0, sizeof(in->path)) ||
        !in->path[0] || !std::memchr(in->cid, 0, sizeof(in->cid)) || std::strlen(in->cid) != 32 ||
        in->parameter_count > JFX_AUDIO_MAX_PLUGIN_PARAMS || jfx_vst3_validate_state(state,bytes)!=JFX_SUCCESS)
        return JFX_ERROR_INVALID_ARGUMENT;
    for (size_t i = 0; i < 32; ++i)
        if (!std::isxdigit((unsigned char) in->cid[i]))
            return JFX_ERROR_INVALID_ARGUMENT;
    FUID cid;
    if (!cid.fromString(in->cid))
        return JFX_ERROR_INVALID_ARGUMENT;
    for (uint32_t i = 0; i < in->parameter_count; ++i) {
        if (!std::isfinite(in->parameters[i].value) || in->parameters[i].value < 0 ||
            in->parameters[i].value > 1)
            return JFX_ERROR_INVALID_ARGUMENT;
        for (uint32_t j = 0; j < i; ++j)
            if (in->parameters[i].id == in->parameters[j].id)
                return JFX_ERROR_INVALID_ARGUMENT;
    }
    auto *p = make<jfx_vst3_instance>();
    if (!p)
        return JFX_ERROR_OUT_OF_MEMORY;
    jfx_result_t result = JFX_ERROR_NOT_IMPLEMENTED;
    p->handler.instance=p;
    try {
        p->module = open_module(in->path);
        if (!p->module)
            result = JFX_ERROR_NOT_FOUND;
        else if (p->module->factory->createInstance(cid.toTUID(), IComponent::iid.toTUID(),
                                                    reinterpret_cast<void **>(&p->component)) ==
                     kResultOk &&
                 p->component &&
                 p->component->initialize(static_cast<IHostApplication *>(&host)) == kResultOk) {
            p->initialized = true;
            if (query(p->component, &p->processor) &&
                p->processor->canProcessSampleSize(kSample32) == kResultOk) {
                query(p->component, &p->controller);
                if (!p->controller) {
                    TUID controller_id{};
                    if (p->component->getControllerClassId(controller_id) == kResultOk &&
                        p->module->factory->createInstance(
                            controller_id, IEditController::iid.toTUID(),
                            reinterpret_cast<void **>(&p->controller)) == kResultOk &&
                        p->controller) {
                        if (p->controller->initialize(static_cast<IHostApplication *>(&host)) !=
                            kResultOk) {
                            p->controller->release();
                            p->controller = nullptr;
                        } else
                            p->controller_initialized = true;
                    }
                }
                if (p->controller) {
                    p->controller->setComponentHandler(&p->handler);
                    if (p->controller_initialized) {
                        query(p->component, &p->component_connection);
                        query(p->controller, &p->controller_connection);
                    }
                    if (p->component_connection && p->controller_connection) {
                        p->component_connection->connect(p->controller_connection);
                        p->controller_connection->connect(p->component_connection);
                        p->connected = true;
                    }
                    Stream stream;
                    if (stream.data && p->component->getState(&stream) == kResultOk) {
                        stream.pos = 0;
                        p->controller->setComponentState(&stream);
                    }
                }
                if (bytes) {
                    const auto *data=static_cast<const unsigned char *>(state);
                    auto u32=[](const unsigned char *b) { return uint32_t(b[0])|(uint32_t(b[1])<<8)|(uint32_t(b[2])<<16)|(uint32_t(b[3])<<24); };
                    uint32_t component_bytes=u32(data+4),controller_bytes=u32(data+8);
                    Stream stream;
                    if (!stream.data) { dispose(p); return JFX_ERROR_OUT_OF_MEMORY; }
                    std::memcpy(stream.data,data+12,component_bytes); stream.length=component_bytes;
                    if (p->component->setState(&stream)!=kResultOk) { dispose(p); return JFX_ERROR_BACKEND_FAILURE; }
                    if (p->controller) {
                        stream.pos=0;
                        if (p->controller->setComponentState(&stream)!=kResultOk) { dispose(p); return JFX_ERROR_BACKEND_FAILURE; }
                        if (controller_bytes) {
                            std::memcpy(stream.data,data+12+component_bytes,controller_bytes); stream.length=controller_bytes; stream.pos=0;
                            if (p->controller->setState(&stream)!=kResultOk) { dispose(p); return JFX_ERROR_BACKEND_FAILURE; }
                        }
                    }
                }
                SpeakerArrangement stereo = SpeakerArr::kStereo;
                BusInfo bi{}, bo{};
                p->instrument=p->component->getBusCount(kAudio,kInput)==0;
                p->midi=p->component->getBusCount(kEvent,kInput)>0;
                if (p->midi) p->component->activateBus(kEvent,kInput,0,true);
                if (p->processor->setBusArrangements(p->instrument?nullptr:&stereo,p->instrument?0:1,&stereo,1) == kResultOk &&
                    p->component->getBusCount(kAudio, kInput) == (p->instrument?0:1) &&
                    p->component->getBusCount(kAudio, kOutput) == 1 &&
                    (p->instrument || (p->component->getBusInfo(kAudio,kInput,0,bi)==kResultOk && bi.channelCount==2)) &&
                    p->component->getBusInfo(kAudio, kOutput, 0, bo) == kResultOk &&
                    bo.channelCount == 2) {
                    p->rate = rate;
                    p->block = block;
                    p->buffers = (float *) alloc((size_t) block * 4 * sizeof(float));
                    if (!p->buffers)
                        result = JFX_ERROR_OUT_OF_MEMORY;
                    else {
                        if (!p->instrument) p->component->activateBus(kAudio, kInput, 0, true);
                        p->component->activateBus(kAudio, kOutput, 0, true);
                        ProcessSetup setup{};
                        setup.processMode = kRealtime;
                        setup.symbolicSampleSize = kSample32;
                        setup.maxSamplesPerBlock = (int32) block;
                        setup.sampleRate = rate;
                        if (p->processor->setupProcessing(setup) == kResultOk) {
                            p->latency = p->processor->getLatencySamples();
                            for (uint32_t i = 0; i < in->parameter_count; ++i) {
                                p->changes.queues[i].id = in->parameters[i].id;
                                p->changes.queues[i].value = in->parameters[i].value;
                                if (p->controller)
                                    p->controller->setParamNormalized(in->parameters[i].id,
                                                                      in->parameters[i].value);
                            }
                            p->changes.count = (int32) in->parameter_count;
                            if (p->latency <= rate * 2 &&
                                p->component->setActive(true) == kResultOk) {
                                p->active = true;
                                if (p->processor->setProcessing(true) == kResultOk) {
                                    p->processing = true;
                                    *out = p;
                                    return JFX_SUCCESS;
                                }
                            }
                        }
                    }
                }
            }
        }
    } catch (...) {
        result = JFX_ERROR_OUT_OF_MEMORY;
    }
    dispose(p);
    return result;
}
extern "C" size_t jfx_vst3_parameter_count(const jfx_vst3_instance_t *p)
{
    if (!p || !p->controller)
        return 0;
    int32 n = p->controller->getParameterCount();
    return n > 0 ? (size_t) n : 0;
}
extern "C" jfx_result_t jfx_vst3_parameter(jfx_vst3_instance_t *p, uint32_t index,
                                           jfx_vst3_parameter_t *out)
{
    if (!p || !out || out->size < sizeof(*out) || index >= jfx_vst3_parameter_count(p))
        return JFX_ERROR_INVALID_ARGUMENT;
    ParameterInfo info{};
    if (p->controller->getParameterInfo((int32) index, info) != kResultOk)
        return JFX_ERROR_BACKEND_FAILURE;
    jfx_vst3_parameter_t value{};
    value.size = sizeof(value);
    value.id = info.id;
    value.steps = info.stepCount;
    value.value = p->controller->getParamNormalized(info.id);
    value.read_only = (info.flags & ParameterInfo::kIsReadOnly) != 0;
    utf8(info.title, value.name, sizeof(value.name));
    utf8(info.units, value.units, sizeof(value.units));
    *out = value;
    return JFX_SUCCESS;
}
extern "C" uint32_t jfx_vst3_latency(const jfx_vst3_instance_t *p)
{
    return p ? p->latency : 0;
}
extern "C" jfx_result_t jfx_vst3_set_tempo(jfx_vst3_instance_t *p, double bpm)
{
    if (!p || !std::isfinite(bpm) || bpm < 20 || bpm > 400)
        return JFX_ERROR_INVALID_ARGUMENT;
    p->tempo = bpm;
    return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_vst3_process(jfx_vst3_instance_t *p, uint64_t sample, size_t frames,
                                         float *pcm)
{ return jfx_vst3_process_events(p,sample,frames,pcm,nullptr,0); }
extern "C" jfx_result_t jfx_vst3_process_events(jfx_vst3_instance_t *p,uint64_t sample,size_t frames,float *pcm,const jfx_midi_event_t *events,size_t count)
{
    if (!p || !pcm || !frames || frames > p->block || sample > (uint64_t) INT64_MAX - frames || count>JFX_VST3_MAX_EVENTS || (count && !events))
        return JFX_ERROR_INVALID_ARGUMENT;
    if (count && !p->midi) return JFX_ERROR_NOT_IMPLEMENTED;
    for (int32 q=0;q<p->changes.count;++q) for (int32 k=0;k<p->changes.queues[q].extra;++k)
        if ((size_t)p->changes.queues[q].points[k].offset>=frames) return JFX_ERROR_INVALID_ARGUMENT;
    p->events.count=0;
    for (size_t i=0;i<count;++i) {
        const auto &n=events[i];
        if (n.size<sizeof(n) || n.sample_offset>=frames || n.channel>15 || n.pitch>127 || !std::isfinite(n.velocity) || n.velocity<0 || n.velocity>1 ||
            (n.type!=JFX_MIDI_NOTE_ON && n.type!=JFX_MIDI_NOTE_OFF) || (i && n.sample_offset<events[i-1].sample_offset)) return JFX_ERROR_INVALID_ARGUMENT;
        Event e{}; e.busIndex=0; e.sampleOffset=(int32)n.sample_offset; e.ppqPosition=(double)(sample+n.sample_offset)*p->tempo/(60*p->rate);
        if (n.type==JFX_MIDI_NOTE_ON) { e.type=Event::kNoteOnEvent; e.noteOn.channel=n.channel; e.noteOn.pitch=n.pitch; e.noteOn.velocity=n.velocity; e.noteOn.noteId=n.note_id; }
        else { e.type=Event::kNoteOffEvent; e.noteOff.channel=n.channel; e.noteOff.pitch=n.pitch; e.noteOff.velocity=n.velocity; e.noteOff.noteId=n.note_id; }
        p->events.values[p->events.count++]=e;
    }
    float *inputs[] = {p->buffers, p->buffers + p->block},
          *outputs[] = {p->buffers + p->block * 2, p->buffers + p->block * 3};
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < 2; ++c) {
            inputs[c][i] = pcm[i * 2 + c];
            outputs[c][i] = 0;
        }
    AudioBusBuffers in{}, out{};
    in.numChannels = out.numChannels = 2;
    in.channelBuffers32 = inputs;
    out.channelBuffers32 = outputs;
    ProcessContext context{};
    context.state = ProcessContext::kPlaying | ProcessContext::kTempoValid |
                    ProcessContext::kTimeSigValid | ProcessContext::kProjectTimeMusicValid |
                    ProcessContext::kBarPositionValid | ProcessContext::kContTimeValid;
    context.sampleRate = p->rate;
    context.projectTimeSamples = (int64) sample;
    context.continousTimeSamples = (int64) sample;
    context.tempo = p->tempo;
    context.timeSigNumerator = 4;
    context.timeSigDenominator = 4;
    context.projectTimeMusic = (double) sample * p->tempo / (60 * p->rate);
    context.barPositionMusic = std::floor(context.projectTimeMusic / 4) * 4;
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = (int32) frames;
    data.numInputs = p->instrument?0:1; data.numOutputs = 1;
    data.inputs = p->instrument?nullptr:&in;
    data.outputs = &out;
    data.processContext = &context;
    data.inputParameterChanges = p->send_changes ? &p->changes : nullptr;
    data.inputEvents=p->midi?&p->events:nullptr;
    if (p->processor->process(data) != kResultOk)
        return JFX_ERROR_BACKEND_FAILURE;
    if (p->processor->getLatencySamples() != p->latency)
        return JFX_ERROR_NOT_IMPLEMENTED;
    p->send_changes = false;
    p->changes.count = 0;
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < 2; ++c)
            if (!std::isfinite(outputs[c][i]))
                return JFX_ERROR_BACKEND_FAILURE;
    for (size_t i = 0; i < frames; ++i)
        for (size_t c = 0; c < 2; ++c)
            pcm[i * 2 + c] = (out.silenceFlags & (1ull << c)) ? 0 : outputs[c][i];
    return JFX_SUCCESS;
}
#include "vst3_features.inc"
#else
extern "C" bool jfx_vst3_available(void)
{
    return false;
}
extern "C" jfx_result_t jfx_vst3_classes(const char *path, jfx_vst3_class_t *out, size_t cap,
                                         size_t *count)
{
    if (!path || !*path || !count || (cap && !out))
        return JFX_ERROR_INVALID_ARGUMENT;
    return JFX_ERROR_NOT_IMPLEMENTED;
}
extern "C" jfx_result_t jfx_vst3_create(const jfx_audio_insert_t *in, uint32_t rate, uint32_t block,
                                        jfx_vst3_instance_t **out)
{
    if (!in || in->size < sizeof(*in) || !out || rate < 8000 || rate > 192000 || !block ||
        block > JFX_AUDIO_MAX_BLOCK_FRAMES)
        return JFX_ERROR_INVALID_ARGUMENT;
    return JFX_ERROR_NOT_IMPLEMENTED;
}
extern "C" void jfx_vst3_destroy(jfx_vst3_instance_t *) {}
extern "C" jfx_result_t jfx_vst3_create_with_state(const jfx_audio_insert_t *in,const void *state,size_t bytes,uint32_t rate,uint32_t block,jfx_vst3_instance_t **out) {
    if (jfx_vst3_validate_state(state,bytes)!=JFX_SUCCESS) return JFX_ERROR_INVALID_ARGUMENT;
    return jfx_vst3_create(in,rate,block,out);
}
extern "C" bool jfx_vst3_is_instrument(const jfx_vst3_instance_t *) { return false; }
extern "C" bool jfx_vst3_accepts_midi(const jfx_vst3_instance_t *) { return false; }
extern "C" jfx_result_t jfx_vst3_save_state(jfx_vst3_instance_t *,void *,size_t,size_t *) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_vst3_process_events(jfx_vst3_instance_t *,uint64_t,size_t,float *,const jfx_midi_event_t *,size_t) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_vst3_set_edit_callback(jfx_vst3_instance_t *,jfx_vst3_edit_fn,void *) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_vst3_set_parameter(jfx_vst3_instance_t *,uint32_t,double) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_vst3_add_parameter_point(jfx_vst3_instance_t *,uint32_t,uint32_t,double) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_vst3_editor_size(jfx_vst3_instance_t *,uint32_t *,uint32_t *) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" jfx_result_t jfx_vst3_editor_open(jfx_vst3_instance_t *,void *,const char *,jfx_vst3_resize_fn,void *) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" void jfx_vst3_editor_close(jfx_vst3_instance_t *) {}
extern "C" jfx_result_t jfx_vst3_editor_resize(jfx_vst3_instance_t *,uint32_t,uint32_t) { return JFX_ERROR_INVALID_ARGUMENT; }
extern "C" void jfx_vst3_editor_focus(jfx_vst3_instance_t *,bool) {}
extern "C" void jfx_vst3_editor_key(jfx_vst3_instance_t *,bool,uint16_t,int16_t,int16_t) {}
extern "C" void jfx_vst3_editor_wheel(jfx_vst3_instance_t *,float) {}
extern "C" void jfx_vst3_pump(void) {}
extern "C" size_t jfx_vst3_parameter_count(const jfx_vst3_instance_t *)
{
    return 0;
}
extern "C" uint32_t jfx_vst3_latency(const jfx_vst3_instance_t *)
{
    return 0;
}
extern "C" jfx_result_t jfx_vst3_set_tempo(jfx_vst3_instance_t *, double)
{
    return JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_vst3_parameter(jfx_vst3_instance_t *, uint32_t, jfx_vst3_parameter_t *)
{
    return JFX_ERROR_INVALID_ARGUMENT;
}
extern "C" jfx_result_t jfx_vst3_process(jfx_vst3_instance_t *, uint64_t, size_t, float *)
{
    return JFX_ERROR_INVALID_ARGUMENT;
}
#endif
