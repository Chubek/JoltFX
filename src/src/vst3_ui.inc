/* Included inside the host namespace. All callbacks use the owner thread. */
class Events final : public IEventList {
public:
    Event values[JFX_VST3_MAX_EVENTS]{};
    int32 count=0;
    tresult PLUGIN_API queryInterface(const TUID id,void **out) override {
        if (!out) return kInvalidArgument;
        *out=nullptr;
        if (!equal(id,IEventList::iid) && !equal(id,FUnknown::iid)) return kNoInterface;
        *out=this; return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return count; }
    tresult PLUGIN_API getEvent(int32 i,Event &out) override { if (i<0 || i>=count) return kInvalidArgument; out=values[i]; return kResultOk; }
    tresult PLUGIN_API addEvent(Event &) override { return kNotImplemented; }
};
class Handler final : public IComponentHandler,public IComponentHandler2 {
public:
    jfx_vst3_instance_t *instance=nullptr;
    jfx_vst3_edit_fn callback=nullptr; void *user=nullptr;
    tresult PLUGIN_API queryInterface(const TUID id,void **out) override {
        if (!out) return kInvalidArgument;
        *out=nullptr;
        if (equal(id,IComponentHandler::iid) || equal(id,FUnknown::iid)) *out=static_cast<IComponentHandler *>(this);
        else if (equal(id,IComponentHandler2::iid)) *out=static_cast<IComponentHandler2 *>(this);
        else return kNoInterface;
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID id) override { if (callback) callback(user,JFX_VST3_EDIT_BEGIN,id,0); return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id,ParamValue v) override {
        auto r=jfx_vst3_set_parameter(instance,id,v);
        if (r!=JFX_SUCCESS) return kResultFalse;
        if (callback) callback(user,JFX_VST3_EDIT_VALUE,id,v);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID id) override { if (callback) callback(user,JFX_VST3_EDIT_END,id,0); return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override {
        if (callback) callback(user,JFX_VST3_EDIT_DIRTY,(uint32_t)flags,0);
        return kResultOk;
    }
    tresult PLUGIN_API setDirty(TBool dirty) override { if (dirty && callback) callback(user,JFX_VST3_EDIT_DIRTY,0,0); return kResultOk; }
    tresult PLUGIN_API requestOpenEditor(FIDString) override { return kNotImplemented; }
    tresult PLUGIN_API startGroupEdit() override { if (callback) callback(user,JFX_VST3_EDIT_BEGIN,0,0); return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { if (callback) callback(user,JFX_VST3_EDIT_END,0,0); return kResultOk; }
};
class PlugFrame final : public IPlugFrame,public Linux::IRunLoop {
public:
    jfx_vst3_resize_fn resize=nullptr; void *user=nullptr;
    struct Timer { Linux::ITimerHandler *handler=nullptr; uint64_t interval=0,next=0; } timers[64];
    struct FD { Linux::IEventHandler *handler=nullptr; int fd=-1; } fds[64];
    PlugFrame *next=nullptr;
    static PlugFrame *head;
    static uint64_t now() { return (uint64_t)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }
    PlugFrame() { next=head; head=this; }
    void clear() {
        for (auto &t:timers) if (t.handler) { auto *h=t.handler; t={}; h->release(); }
        for (auto &f:fds) if (f.handler) { auto *h=f.handler; f={}; h->release(); }
    }
    ~PlugFrame() { clear(); auto **p=&head; while (*p && *p!=this) p=&(*p)->next; if (*p) *p=next; }
    tresult PLUGIN_API queryInterface(const TUID id,void **out) override {
        if (!out) return kInvalidArgument;
        *out=nullptr;
        if (equal(id,IPlugFrame::iid) || equal(id,FUnknown::iid)) *out=static_cast<IPlugFrame *>(this);
        else if (equal(id,Linux::IRunLoop::iid)) *out=static_cast<Linux::IRunLoop *>(this);
        else return kNoInterface;
        return kResultOk;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API resizeView(IPlugView *view,ViewRect *rect) override {
        if (!view || !rect || rect->getWidth()<1 || rect->getHeight()<1 || rect->getWidth()>8192 || rect->getHeight()>8192 || !resize) return kInvalidArgument;
        if (resize(user,(uint32_t)rect->getWidth(),(uint32_t)rect->getHeight())!=JFX_SUCCESS) return kResultFalse;
        return view->onSize(rect);
    }
    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler *h,int fd) override {
        if (!h || fd<0) return kInvalidArgument;
        for (auto &f:fds) if (f.handler==h && f.fd==fd) return kResultOk;
        for (auto &f:fds) if (!f.handler) { h->addRef(); f={h,fd}; return kResultOk; }
        return kResultFalse;
    }
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler *h) override {
        bool found=false;
        for (auto &f:fds) if (h && f.handler==h) { f={}; h->release(); found=true; }
        return found?kResultOk:kInvalidArgument;
    }
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler *h,uint64_t interval) override {
        if (!h || !interval || interval>86400000) return kInvalidArgument;
        for (auto &t:timers) if (t.handler==h) { t.interval=interval; t.next=now()+interval; return kResultOk; }
        for (auto &t:timers) if (!t.handler) { h->addRef(); t={h,interval,now()+interval}; return kResultOk; }
        return kResultFalse;
    }
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler *h) override {
        for (auto &t:timers) if (h && t.handler==h) { t={}; h->release(); return kResultOk; }
        return kInvalidArgument;
    }
    void pump() {
        uint64_t clock=now();
        for (auto &t:timers) if (t.handler && clock>=t.next) { auto *h=t.handler; t.next=clock+t.interval; h->addRef(); h->onTimer(); h->release(); }
#if !defined(_WIN32) && !defined(__APPLE__)
        for (auto &f:fds) if (f.handler) { pollfd p{f.fd,POLLIN,0}; if (poll(&p,1,0)>0 && (p.revents&(POLLIN|POLLHUP|POLLERR))) { auto *h=f.handler; h->addRef(); h->onFDIsSet(f.fd); h->release(); } }
#endif
    }
};
PlugFrame *PlugFrame::head=nullptr;
