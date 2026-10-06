/* A real factory/processor/controller module, independent of the host. */
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
#include <cassert>
#include <cstring>
#include <new>
using namespace Steinberg;
using namespace Steinberg::Vst;
namespace Steinberg
{
DEF_CLASS_IID(IPlugView)
DEF_CLASS_IID(IPlugFrame)
namespace Linux { DEF_CLASS_IID(IRunLoop) DEF_CLASS_IID(ITimerHandler) }
namespace Vst
{
DEF_CLASS_IID(IComponent)
DEF_CLASS_IID(IAudioProcessor)
DEF_CLASS_IID(IEditController)
DEF_CLASS_IID(IHostApplication)
DEF_CLASS_IID(IMessage)
DEF_CLASS_IID(IAttributeList)
DEF_CLASS_IID(IConnectionPoint)
} // namespace Vst
} // namespace Steinberg
static const FUID gain_id(0x12345678, 0x11223344, 0x55667788, 0x01020304);
static const FUID delay_id(0x12345678, 0x11223344, 0x55667788, 0x01020305);
static const FUID mono_id(0x12345678, 0x11223344, 0x55667788, 0x01020306);
static const FUID separate_id(0x12345678, 0x11223344, 0x55667788, 0x01020307);
static const FUID controller_id(0x12345678, 0x11223344, 0x55667788, 0x01020308);
static const FUID instrument_id(0x12345678,0x11223344,0x55667788,0x01020309);
static const FUID instrument_delay_id(0x12345678,0x11223344,0x55667788,0x0102030a);
static bool entered = false;
static int live = 0;
static bool eq(const TUID uid, const FUID &id)
{
    return !std::memcmp(uid, id.toTUID(), 16);
}
class Effect final : public IComponent,
                     public IAudioProcessor,
                     public IEditController,
                     public IConnectionPoint
{
  public:
    uint32 refs = 1, delay = 0;
    bool mono = false, initialized = false, active = false, processing = false, setup = false;
    bool separate = false;
    bool instrument=false;
    float voices[128]{};
    IComponentHandler *handler=nullptr;
    IConnectionPoint *peer = nullptr;
    double gain = 1;
    bool changed_latency = false;
    float ring[3][2]{};
    uint32 cursor = 0;
    explicit Effect(uint32 latency, bool single) : delay(latency), mono(single)
    {
        ++live;
    }
    ~Effect()
    {
        assert(!initialized && !active && !processing && !peer);
        --live;
    }
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override
    {
        *out = nullptr;
        if (eq(id, IComponent::iid) || eq(id, FUnknown::iid) || eq(id, IPluginBase::iid))
            *out = static_cast<IComponent *>(this);
        else if (eq(id, IAudioProcessor::iid))
            *out = static_cast<IAudioProcessor *>(this);
        else if (eq(id, IEditController::iid) && !separate)
            *out = static_cast<IEditController *>(this);
        else if (eq(id, IConnectionPoint::iid))
            *out = static_cast<IConnectionPoint *>(this);
        else
            return kNoInterface;
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
            delete this;
        return n;
    }
    tresult PLUGIN_API initialize(FUnknown *host) override
    {
        assert(host && !initialized && entered);
        IHostApplication *app = nullptr;
        assert(host->queryInterface(IHostApplication::iid.toTUID(),
                                    reinterpret_cast<void **>(&app)) == kResultOk);
        String128 name{};
        assert(app->getName(name) == kResultOk && name[0] == 'J');
        IMessage *message = nullptr;
        TUID uid;
        IMessage::iid.toTUID(uid);
        assert(app->createInstance(uid, uid, reinterpret_cast<void **>(&message)) == kResultOk);
        message->setMessageID("test");
        assert(!std::strcmp(message->getMessageID(), "test"));
        auto *attrs = message->getAttributes();
        attrs->addRef();
        assert(attrs->setInt("int", 42) == kResultOk);
        int64 integer = 0;
        assert(attrs->getInt("int", integer) == kResultOk && integer == 42);
        assert(attrs->setFloat("float", .25) == kResultOk);
        double floating = 0;
        assert(attrs->getFloat("float", floating) == kResultOk && floating == .25);
        TChar text[] = {'o', 'k', 0}, copy[3]{};
        assert(attrs->setString("string", text) == kResultOk &&
               attrs->getString("string", copy, sizeof(copy)) == kResultOk && copy[0] == 'o');
        const void *binary = nullptr;
        uint32 bytes = 0;
        assert(attrs->setBinary("binary", text, sizeof(text)) == kResultOk &&
               attrs->getBinary("binary", binary, bytes) == kResultOk && bytes == sizeof(text) &&
               !std::memcmp(binary, text, bytes));
        message->release();
        assert(attrs->getInt("int", integer) == kResultOk);
        attrs->release();
        app->release();
        initialized = true;
        return kResultOk;
    }
    tresult PLUGIN_API terminate() override
    {
        assert(initialized && !active);
        initialized = false;
        return kResultOk;
    }
    tresult PLUGIN_API getControllerClassId(TUID out) override
    {
        if (separate) {
            controller_id.toTUID(out);
            return kResultOk;
        }
        return kResultFalse;
    }
    tresult PLUGIN_API setIoMode(IoMode) override
    {
        return kResultOk;
    }
    int32 PLUGIN_API getBusCount(MediaType type, BusDirection dir) override
    {
        if (instrument) return type==kEvent?(dir==kInput?1:0):(dir==kInput?0:1);
        return type == kAudio ? 1 : 0;
    }
    tresult PLUGIN_API getBusInfo(MediaType type, BusDirection dir, int32 i, BusInfo &out) override
    {
        if (type != kAudio || i)
            return kInvalidArgument;
        out = {};
        out.mediaType = kAudio;
        out.direction = dir;
        out.channelCount = mono ? 1 : 2;
        out.busType = kMain;
        out.flags = BusInfo::kDefaultActive;
        return kResultOk;
    }
    tresult PLUGIN_API getRoutingInfo(RoutingInfo &, RoutingInfo &) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API activateBus(MediaType, BusDirection, int32, TBool) override
    {
        return kResultOk;
    }
    tresult PLUGIN_API setActive(TBool state) override
    {
        assert(initialized && !processing);
        if (state)
            assert(setup);
        active = state != 0;
        return kResultOk;
    }
    tresult PLUGIN_API setState(IBStream *stream) override
    {
        return stream->read(&gain, sizeof(gain), nullptr);
    }
    tresult PLUGIN_API getState(IBStream *stream) override
    {
        return stream->write(&gain, sizeof(gain), nullptr);
    }
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement *in, int32 ni, SpeakerArrangement *out,
                                          int32 no) override
    {
        assert(initialized && !active);
        return !mono && ni == (instrument?0:1) && no == 1 && (instrument || *in == SpeakerArr::kStereo) &&
                       *out == SpeakerArr::kStereo
                   ? kResultOk
                   : kResultFalse;
    }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32 i, SpeakerArrangement &out) override
    {
        if (i)
            return kInvalidArgument;
        out = SpeakerArr::kStereo;
        return kResultOk;
    }
    tresult PLUGIN_API canProcessSampleSize(int32 size) override
    {
        return size == kSample32 ? kResultOk : kResultFalse;
    }
    uint32 PLUGIN_API getLatencySamples() override
    {
        return changed_latency ? 1u : delay;
    }
    tresult PLUGIN_API setupProcessing(ProcessSetup &s) override
    {
        assert(initialized && !active && s.sampleRate >= 8000 && s.maxSamplesPerBlock > 0);
        setup = true;
        return kResultOk;
    }
    tresult PLUGIN_API setProcessing(TBool state) override
    {
        assert(active);
        processing = state != 0;
        return kResultOk;
    }
    tresult PLUGIN_API process(ProcessData &d) override
    {
        assert(processing && d.numInputs == (instrument?0:1) && d.numOutputs == 1 && d.processContext &&
               d.symbolicSampleSize == kSample32);
        assert(d.processContext->tempo >= 20 && d.processContext->timeSigNumerator == 4);
        IParamValueQueue *gain_queue=nullptr;
        if (d.inputParameterChanges)
            for (int32 i = 0; i < d.inputParameterChanges->getParameterCount(); ++i) {
                auto *q = d.inputParameterChanges->getParameterData(i);
                if (!q->getParameterId()) gain_queue=q;
                int32 offset = 0;
                double value = 0;
                if (!q->getParameterId() && q->getPoint(0, offset, value) == kResultOk)
                    gain = value;
                if (q->getParameterId() == 1 && q->getPoint(0, offset, value) == kResultOk &&
                    value > 0)
                    changed_latency = true;
            }
        for (int32 i = 0; i < d.numSamples; ++i) {
            if (gain_queue) {
                int32 left_offset=0; double left_value=gain; gain_queue->getPoint(0,left_offset,left_value);
                gain=left_value;
                for (int32 point=1;point<gain_queue->getPointCount();++point) {
                    int32 right_offset=0; double right_value=0; assert(gain_queue->getPoint(point,right_offset,right_value)==kResultOk);
                    if (i<right_offset) { gain=left_value+(right_value-left_value)*(double)(i-left_offset)/(double)(right_offset-left_offset); break; }
                    gain=right_value; left_offset=right_offset; left_value=right_value;
                }
            }
            if (instrument && d.inputEvents) for (int32 e=0;e<d.inputEvents->getEventCount();++e) {
                Event ev{}; assert(d.inputEvents->getEvent(e,ev)==kResultOk);
                if (ev.sampleOffset!=i) continue;
                if (ev.type==Event::kNoteOnEvent) voices[ev.noteOn.pitch]=ev.noteOn.velocity;
                else if (ev.type==Event::kNoteOffEvent) voices[ev.noteOff.pitch]=0;
            }
            for (int32 c = 0; c < 2; ++c) {
                float input=0;
                if (instrument) { for (float v:voices) input+=v*(float)gain; }
                else input=d.inputs[0].channelBuffers32[c][i]*(float)gain;
                if (delay) {
                    d.outputs[0].channelBuffers32[c][i] = ring[cursor][c];
                    ring[cursor][c] = input;
                } else
                    d.outputs[0].channelBuffers32[c][i] = input;
            }
            if (delay)
                cursor = (cursor + 1) % delay;
        }
        return kResultOk;
    }
    uint32 PLUGIN_API getTailSamples() override
    {
        return 0;
    }
    tresult PLUGIN_API setComponentState(IBStream *stream) override
    {
        return setState(stream);
    }
    int32 PLUGIN_API getParameterCount() override
    {
        return 2;
    }
    tresult PLUGIN_API getParameterInfo(int32 i, ParameterInfo &out) override
    {
        if (i < 0 || i > 1)
            return kInvalidArgument;
        out = {};
        out.id = (ParamID) i;
        out.stepCount = i ? 1 : 0;
        out.defaultNormalizedValue = i ? 0 : 1;
        out.flags = ParameterInfo::kCanAutomate;
        const char *name = i ? "Runtime latency" : "Gain";
        for (size_t n = 0; name[n]; ++n)
            out.title[n] = (TChar) name[n];
        return kResultOk;
    }
    tresult PLUGIN_API getParamStringByValue(ParamID, ParamValue, String128) override
    {
        return kNotImplemented;
    }
    tresult PLUGIN_API getParamValueByString(ParamID, TChar *, ParamValue &) override
    {
        return kNotImplemented;
    }
    ParamValue PLUGIN_API normalizedParamToPlain(ParamID, ParamValue v) override
    {
        return v;
    }
    ParamValue PLUGIN_API plainParamToNormalized(ParamID, ParamValue v) override
    {
        return v;
    }
    ParamValue PLUGIN_API getParamNormalized(ParamID id) override
    {
        return id ? 0 : gain;
    }
    tresult PLUGIN_API setParamNormalized(ParamID id, ParamValue v) override
    {
        if (id == 1)
            return kResultOk;
        if (id)
            return kInvalidArgument;
        gain = v;
        return kResultOk;
    }
    tresult PLUGIN_API setComponentHandler(IComponentHandler *h) override
    {
        handler=h;
        return kResultOk;
    }
    IPlugView *PLUGIN_API createView(FIDString) override;
    tresult PLUGIN_API connect(IConnectionPoint *other) override
    {
        assert(!peer && other);
        peer = other;
        return kResultOk;
    }
    tresult PLUGIN_API disconnect(IConnectionPoint *other) override
    {
        assert(peer == other);
        peer = nullptr;
        return kResultOk;
    }
    tresult PLUGIN_API notify(IMessage *) override
    {
        return kResultOk;
    }
};
class TestView final : public IPlugView,public Linux::ITimerHandler {
public:
    uint32 refs=1; Effect *owner; IPlugFrame *frame=nullptr; Linux::IRunLoop *loop=nullptr; bool is_attached=false;
    explicit TestView(Effect *o):owner(o) { owner->addRef(); }
    ~TestView() { assert(!is_attached && !frame && !loop); owner->release(); }
    tresult PLUGIN_API queryInterface(const TUID id,void **out) override {
        *out=nullptr;
        if (eq(id,IPlugView::iid) || eq(id,FUnknown::iid)) *out=static_cast<IPlugView *>(this);
        else if (eq(id,Linux::ITimerHandler::iid)) *out=static_cast<Linux::ITimerHandler *>(this);
        else return kNoInterface;
        addRef(); return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return ++refs; }
    uint32 PLUGIN_API release() override { auto n=--refs; if (!n) delete this; return n; }
    tresult PLUGIN_API isPlatformTypeSupported(FIDString type) override { return !std::strcmp(type,"X11EmbedWindowID") || !std::strcmp(type,"HWND") || !std::strcmp(type,"NSView")?kResultTrue:kResultFalse; }
    tresult PLUGIN_API attached(void *parent,FIDString type) override {
        assert(parent && frame && isPlatformTypeSupported(type)==kResultTrue); is_attached=true;
        assert(frame->queryInterface(Linux::IRunLoop::iid.toTUID(),reinterpret_cast<void **>(&loop))==kResultOk);
        assert(loop->registerTimer(this,1)==kResultOk);
        ViewRect rect(0,0,420,240); assert(frame->resizeView(this,&rect)==kResultOk); return kResultOk;
    }
    tresult PLUGIN_API removed() override { assert(is_attached); is_attached=false; assert(loop->unregisterTimer(this)==kResultOk); loop->release(); loop=nullptr; return kResultOk; }
    tresult PLUGIN_API onWheel(float) override {
        assert(owner->handler); owner->handler->beginEdit(0); owner->handler->performEdit(0,.25); owner->handler->endEdit(0); return kResultOk;
    }
    tresult PLUGIN_API onKeyDown(char16,int16,int16) override { return kResultOk; }
    tresult PLUGIN_API onKeyUp(char16,int16,int16) override { return kResultOk; }
    tresult PLUGIN_API getSize(ViewRect *r) override { *r=ViewRect(0,0,400,200); return kResultOk; }
    tresult PLUGIN_API onSize(ViewRect *r) override { assert(r && r->getWidth()>0); return kResultOk; }
    tresult PLUGIN_API onFocus(TBool) override { return kResultOk; }
    tresult PLUGIN_API setFrame(IPlugFrame *f) override { if (frame) frame->release(); frame=f; if (f) f->addRef(); return kResultOk; }
    tresult PLUGIN_API canResize() override { return kResultTrue; }
    tresult PLUGIN_API checkSizeConstraint(ViewRect *) override { return kResultOk; }
    void PLUGIN_API onTimer() override { assert(is_attached && loop); }
};
IPlugView *Effect::createView(FIDString type) { return !std::strcmp(type,ViewType::kEditor)?new TestView(this):nullptr; }
class Factory final : public IPluginFactory
{
  public:
    tresult PLUGIN_API queryInterface(const TUID id, void **out) override
    {
        *out = nullptr;
        if (!eq(id, IPluginFactory::iid) && !eq(id, FUnknown::iid))
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
    tresult PLUGIN_API getFactoryInfo(PFactoryInfo *out) override
    {
        *out = {};
        return kResultOk;
    }
    int32 PLUGIN_API countClasses() override
    {
        return 7;
    }
    tresult PLUGIN_API getClassInfo(int32 i, PClassInfo *out) override
    {
        if (i < 0 || i > 6)
            return kInvalidArgument;
        *out = {};
        (i == 0   ? gain_id
         : i == 1 ? delay_id
         : i == 2 ? mono_id
         : i == 3 ? separate_id
         : i == 4 ? controller_id : i==5?instrument_id:instrument_delay_id)
            .toTUID(out->cid);
        std::strcpy(out->category, i == 4 ? "Component Controller Class" : kVstAudioEffectClass);
        std::strcpy(out->name, i == 0   ? "Test Gain"
                               : i == 1 ? "Test Delay"
                               : i == 2 ? "Unsupported Mono"
                               : i == 5 ? "Test Instrument" : "Separate Controller");
        return kResultOk;
    }
    tresult PLUGIN_API createInstance(FIDString cid, FIDString iid, void **out) override
    {
        if (!eq(cid, gain_id) && !eq(cid, delay_id) && !eq(cid, mono_id) && !eq(cid, separate_id) &&
            !eq(cid, controller_id) && !eq(cid,instrument_id) && !eq(cid,instrument_delay_id))
            return kInvalidArgument;
        auto *p = new (std::nothrow) Effect(eq(cid, delay_id) || eq(cid,instrument_delay_id) ? 3u : 0u, eq(cid, mono_id));
        if (!p)
            return kOutOfMemory;
        p->separate = eq(cid, separate_id);
        p->instrument=eq(cid,instrument_id) || eq(cid,instrument_delay_id);
        auto r = p->queryInterface(iid, out);
        p->release();
        return r;
    }
};
static Factory factory;
#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
EXPORT bool InitDll()
{
    assert(!entered);
    entered = true;
    return true;
}
EXPORT bool ExitDll()
{
    assert(entered && !live);
    entered = false;
    return true;
}
#elif defined(__APPLE__)
#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT bool bundleEntry(void *)
{
    assert(!entered);
    entered = true;
    return true;
}
EXPORT bool bundleExit()
{
    assert(entered && !live);
    entered = false;
    return true;
}
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
EXPORT bool ModuleEntry(void *)
{
    assert(!entered);
    entered = true;
    return true;
}
EXPORT bool ModuleExit()
{
    assert(entered && !live);
    entered = false;
    return true;
}
#endif
EXPORT IPluginFactory *PLUGIN_API GetPluginFactory()
{
    return &factory;
}
