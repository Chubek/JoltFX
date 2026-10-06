extern "C" bool jfx_vst3_is_instrument(const jfx_vst3_instance_t *p) { return p && p->instrument; }
extern "C" bool jfx_vst3_accepts_midi(const jfx_vst3_instance_t *p) { return p && p->midi; }
extern "C" jfx_result_t jfx_vst3_save_state(jfx_vst3_instance_t *p,void *out,size_t capacity,size_t *bytes) {
    if (!p || !bytes || (capacity && !out)) return JFX_ERROR_INVALID_ARGUMENT;
    Stream component,controller;
    if (!component.data || !controller.data) return JFX_ERROR_OUT_OF_MEMORY;
    if (p->component->getState(&component)!=kResultOk) return JFX_ERROR_NOT_IMPLEMENTED;
    if (p->controller && p->controller->getState(&controller)!=kResultOk) controller.length=0;
    if (component.failed || controller.failed) return JFX_ERROR_OUT_OF_MEMORY;
    *bytes=(size_t)(component.length+controller.length)+12;
    if (!out && !capacity) return JFX_SUCCESS;
    if (capacity<*bytes) return JFX_ERROR_OUT_OF_MEMORY;
    auto *data=static_cast<unsigned char *>(out); std::memcpy(data,"JVS1",4);
    for (unsigned b=0;b<4;++b) { data[4+b]=(unsigned char)((uint32_t)component.length>>(b*8)); data[8+b]=(unsigned char)((uint32_t)controller.length>>(b*8)); }
    std::memcpy(data+12,component.data,(size_t)component.length);
    std::memcpy(data+12+component.length,controller.data,(size_t)controller.length);
    return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_vst3_set_edit_callback(jfx_vst3_instance_t *p,jfx_vst3_edit_fn callback,void *user) {
    if (!p) return JFX_ERROR_INVALID_ARGUMENT;
    p->handler.callback=callback; p->handler.user=user; return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_vst3_set_parameter(jfx_vst3_instance_t *p,uint32_t id,double value) {
    if (!p || !std::isfinite(value) || value<0 || value>1) return JFX_ERROR_INVALID_ARGUMENT;
    int32 index=0;
    while (index<p->changes.count && p->changes.queues[index].id!=id) ++index;
    if (index==p->changes.count && index>=(int32)JFX_VST3_MAX_PARAM_QUEUES) return JFX_ERROR_OUT_OF_MEMORY;
    if (p->controller && p->controller->setParamNormalized(id,value)!=kResultOk) return JFX_ERROR_INVALID_ARGUMENT;
    if (index==p->changes.count) { ++p->changes.count; p->changes.queues[index].extra=0; }
    p->changes.queues[index].id=id; p->changes.queues[index].value=value; p->changes.queues[index].extra=0; p->send_changes=true; return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_vst3_add_parameter_point(jfx_vst3_instance_t *p,uint32_t id,uint32_t offset,double value) {
    if (!p || !std::isfinite(value) || value<0 || value>1 || offset>=p->block) return JFX_ERROR_INVALID_ARGUMENT;
    int32 index=0; while (index<p->changes.count && p->changes.queues[index].id!=id) ++index;
    if (index==p->changes.count) {
        if (index>=(int32)JFX_VST3_MAX_PARAM_QUEUES) return JFX_ERROR_OUT_OF_MEMORY;
        ++p->changes.count; p->changes.queues[index].id=id; p->changes.queues[index].extra=0; p->changes.queues[index].value=p->controller?p->controller->getParamNormalized(id):value;
    }
    Queue &q=p->changes.queues[index];
    if (!offset) { if (q.extra) return JFX_ERROR_INVALID_ARGUMENT; q.value=value; }
    else {
        if (q.extra && offset<=(uint32_t)q.points[q.extra-1].offset) return JFX_ERROR_INVALID_ARGUMENT;
        if (q.extra>=511) return JFX_ERROR_OUT_OF_MEMORY;
        q.points[q.extra++]={(int32)offset,value};
    }
    p->send_changes=true; return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_vst3_editor_size(jfx_vst3_instance_t *p,uint32_t *width,uint32_t *height) {
    if (!p || !width || !height) return JFX_ERROR_INVALID_ARGUMENT;
    if (!p->view && p->controller) p->view=p->controller->createView(ViewType::kEditor);
    if (!p->view) return JFX_ERROR_NOT_IMPLEMENTED;
    ViewRect rect;
    if (p->view->getSize(&rect)!=kResultOk || rect.getWidth()<1 || rect.getHeight()<1 || rect.getWidth()>8192 || rect.getHeight()>8192) return JFX_ERROR_BACKEND_FAILURE;
    *width=(uint32_t)rect.getWidth(); *height=(uint32_t)rect.getHeight(); return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_vst3_editor_open(jfx_vst3_instance_t *p,void *parent,const char *platform,jfx_vst3_resize_fn resize,void *user) {
    if (!p || !parent || !platform || !resize || p->attached) return JFX_ERROR_INVALID_ARGUMENT;
    uint32_t width,height; auto r=jfx_vst3_editor_size(p,&width,&height); if (r!=JFX_SUCCESS) return r;
    if (p->view->isPlatformTypeSupported(platform)!=kResultTrue) return JFX_ERROR_NOT_IMPLEMENTED;
    p->frame.resize=resize; p->frame.user=user;
    if (p->view->setFrame(&p->frame)!=kResultOk || p->view->attached(parent,platform)!=kResultOk) { p->view->setFrame(nullptr); p->frame.clear(); return JFX_ERROR_BACKEND_FAILURE; }
    p->attached=true; return JFX_SUCCESS;
}
extern "C" void jfx_vst3_editor_close(jfx_vst3_instance_t *p) {
    if (!p) return;
    if (p->view) { if (p->attached) p->view->removed(); p->view->setFrame(nullptr); p->view->release(); p->view=nullptr; }
    p->attached=false; p->frame.clear(); p->frame.resize=nullptr; p->frame.user=nullptr;
}
extern "C" jfx_result_t jfx_vst3_editor_resize(jfx_vst3_instance_t *p,uint32_t width,uint32_t height) {
    if (!p || !p->attached || !width || !height || width>8192 || height>8192) return JFX_ERROR_INVALID_ARGUMENT;
    ViewRect rect(0,0,(int32)width,(int32)height);
    if (p->view->canResize()!=kResultTrue) return JFX_ERROR_NOT_IMPLEMENTED;
    if (p->view->checkSizeConstraint(&rect)!=kResultOk) return JFX_ERROR_BACKEND_FAILURE;
    return p->frame.resizeView(p->view,&rect)==kResultOk?JFX_SUCCESS:JFX_ERROR_BACKEND_FAILURE;
}
extern "C" void jfx_vst3_editor_focus(jfx_vst3_instance_t *p,bool focused) { if (p && p->attached) p->view->onFocus(focused); }
extern "C" void jfx_vst3_editor_key(jfx_vst3_instance_t *p,bool down,uint16_t ch,int16_t key,int16_t modifiers) {
    if (p && p->attached) { if (down) p->view->onKeyDown((char16)ch,key,modifiers); else p->view->onKeyUp((char16)ch,key,modifiers); }
}
extern "C" void jfx_vst3_editor_wheel(jfx_vst3_instance_t *p,float distance) { if (p && p->attached && std::isfinite(distance)) p->view->onWheel(distance); }
extern "C" void jfx_vst3_pump(void) { for (auto *f=PlugFrame::head;f;f=f->next) f->pump(); }
