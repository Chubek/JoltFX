#include "renderer.h"
#include "surface_material.h"

#include <filament/Camera.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/IndirectLight.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <backend/PixelBufferDescriptor.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

namespace {
using namespace filament;
using namespace filament::math;
struct Vertex { float3 position; quatf tangent; };
struct Draw {
    utils::Entity entity;
    VertexBuffer *vertices=nullptr;
    IndexBuffer *indices=nullptr;
    MaterialInstance *material=nullptr;
    Vertex *data=nullptr;
    uint32_t *index_data=nullptr;
};
struct Frame {
    const jfx_filament_desc_t &desc;
    Engine *engine=nullptr;
    Scene *scene=nullptr;
    View *view=nullptr;
    Renderer *renderer=nullptr;
    SwapChain *swap=nullptr;
    Material *material=nullptr;
    IndirectLight *ambient=nullptr;
    utils::Entity camera,light;
    Draw *draws=nullptr;
    uint32_t count=0;
    uint8_t *pixels=nullptr;
    explicit Frame(const jfx_filament_desc_t &d):desc(d) {}
    ~Frame() {
        // Driver commands borrow all upload/readback buffers until completion.
        // Teardown drains the SDK before any caller-owned storage is released.
        if (engine) {
            try { engine->flushAndWait(); } catch (...) { }
            for (uint32_t i=0;i<count;++i) {
                auto &d=draws[i];
                engine->destroy(d.entity);
                engine->destroy(d.material); engine->destroy(d.vertices); engine->destroy(d.indices);
                utils::EntityManager::get().destroy(d.entity);
            }
            engine->destroy(light); utils::EntityManager::get().destroy(light);
            engine->destroyCameraComponent(camera); utils::EntityManager::get().destroy(camera);
            engine->destroy(view); engine->destroy(renderer); engine->destroy(scene);
            engine->destroy(ambient); engine->destroy(material); engine->destroy(swap);
            Engine::destroy(&engine);
        }
        for (uint32_t i=0;i<count;++i) {
            desc.release(draws[i].data); desc.release(draws[i].index_data);
            std::destroy_at(&draws[i]);
        }
        desc.release(draws); desc.release(pixels);
    }
};
float3 vector(const float *p) { return {p[0],p[1],p[2]}; }
bool finite(const float *p,size_t n) {
    for (size_t i=0;i<n;++i) if (!std::isfinite(p[i])) return false;
    return true;
}
}

extern "C" __attribute__((visibility("default"))) int jfx_filament_render(
        const jfx_filament_desc_t *desc,uint8_t *out,size_t capacity) {
    if (!desc || desc->size<sizeof(*desc) || !out || !desc->allocate || !desc->release ||
        !desc->width || !desc->height || desc->width>2048 || desc->height>2048 ||
        capacity<size_t(desc->width)*desc->height*4 || desc->draw_count>4096 ||
        (desc->draw_count && !desc->draws) || !finite(desc->eye,3) || !finite(desc->target,3) ||
        !finite(desc->up,3) || !(desc->vertical_fov>=10 && desc->vertical_fov<=120)) return -1;
    size_t total=0;
    for (uint32_t i=0;i<desc->draw_count;++i) {
        auto &d=desc->draws[i]; total+=d.vertex_count;
        if (!d.vertices || !d.vertex_count || d.vertex_count%3 || total>6u*1024u*1024u ||
            !finite(d.model,16) || !finite(d.color,3) || !finite(d.vertices,size_t(d.vertex_count)*6)) return -1;
    }
    try {
        std::atomic<bool> complete{false};
        Frame f(*desc);
        Engine::Config config; config.jobSystemThreadCount=Engine::Config::SINGLE_THREADED;
        Engine::Builder builder;
        // A frame-scoped engine does not benefit from a separate shader pool.
        // Compile on its driver thread, keeping shared-context teardown ordered.
        builder.config(&config).feature("backend.disable_parallel_shader_compile",true);
#ifdef __APPLE__
        f.engine=builder.backend(Engine::Backend::METAL).build();
#else
        f.engine=builder.backend(Engine::Backend::OPENGL).build();
#endif
        if (!f.engine) return -4;
        auto &engine=*f.engine;
        f.scene=engine.createScene(); f.view=engine.createView(); f.renderer=engine.createRenderer();
        f.swap=engine.createSwapChain(desc->width,desc->height);
        f.material=Material::Builder().package(surface_material,sizeof(surface_material)).build(engine);
        if (!f.material) return -4;
        f.camera=utils::EntityManager::get().create();
        auto *camera=engine.createCamera(f.camera);
        camera->setProjection(desc->vertical_fov,double(desc->width)/desc->height,.05,100000,Camera::Fov::VERTICAL);
        camera->lookAt(vector(desc->eye),vector(desc->target),vector(desc->up));
        camera->setExposure(16,1.f/125,100);
        f.view->setCamera(camera); f.view->setScene(f.scene); f.view->setViewport({0,0,desc->width,desc->height});
        f.view->setAntiAliasing(AntiAliasing::FXAA);
        f.light=utils::EntityManager::get().create();
        LightManager::Builder(LightManager::Type::DIRECTIONAL).color({1,1,1}).intensity(100000)
            .direction({-.4f,-.8f,-.3f}).castShadows(true).build(engine,f.light);
        f.scene->addEntity(f.light);
        const float3 sh[]={{.3f,.3f,.3f}};
        f.ambient=IndirectLight::Builder().irradiance(1,sh).intensity(30000).build(engine);
        f.scene->setIndirectLight(f.ambient);
        f.renderer->setClearOptions({.clearColor={.086f,.102f,.133f,1},.clear=true,.discard=true});
        f.draws=static_cast<Draw *>(desc->allocate(size_t(desc->draw_count)*sizeof(Draw)));
        if (desc->draw_count && !f.draws) return -2;
        for (uint32_t i=0;i<desc->draw_count;++i) {
            auto &input=desc->draws[i];
            auto &d=*std::construct_at(f.draws+i); ++f.count;
            d.data=static_cast<Vertex *>(desc->allocate(size_t(input.vertex_count)*sizeof(Vertex)));
            d.index_data=static_cast<uint32_t *>(desc->allocate(size_t(input.vertex_count)*sizeof(uint32_t)));
            if (!d.data || !d.index_data) return -2;
            float3 minimum=vector(input.vertices),maximum=minimum;
            for (uint32_t j=0;j<input.vertex_count;++j) {
                auto p=vector(input.vertices+j*6),n=vector(input.vertices+j*6+3);
                n=length(n)>1e-10f?normalize(n):float3{0,1,0};
                auto tangent=normalize(cross(std::abs(n.y)<.9f?float3{0,1,0}:float3{1,0,0},n));
                d.data[j]={p,mat3f::packTangentFrame(mat3f{tangent,cross(n,tangent),n})};
                d.index_data[j]=j; minimum=min(minimum,p); maximum=max(maximum,p);
            }
            d.vertices=VertexBuffer::Builder().vertexCount(input.vertex_count).bufferCount(1)
                .attribute(VertexAttribute::POSITION,0,VertexBuffer::AttributeType::FLOAT3,offsetof(Vertex,position),sizeof(Vertex))
                .attribute(VertexAttribute::TANGENTS,0,VertexBuffer::AttributeType::FLOAT4,offsetof(Vertex,tangent),sizeof(Vertex))
                .build(engine);
            d.indices=IndexBuffer::Builder().indexCount(input.vertex_count).bufferType(IndexBuffer::IndexType::UINT).build(engine);
            d.vertices->setBufferAt(engine,0,VertexBuffer::BufferDescriptor(d.data,size_t(input.vertex_count)*sizeof(Vertex)));
            d.indices->setBuffer(engine,IndexBuffer::BufferDescriptor(d.index_data,size_t(input.vertex_count)*sizeof(uint32_t)));
            d.material=f.material->createInstance(); d.material->setParameter("baseColor",vector(input.color));
            d.entity=utils::EntityManager::get().create();
            auto result=RenderableManager::Builder(1).boundingBox({(maximum+minimum)*.5f,(maximum-minimum)*.5f})
                .material(0,d.material).geometry(0,RenderableManager::PrimitiveType::TRIANGLES,d.vertices,d.indices)
                .culling(false).receiveShadows(true).castShadows(true).build(engine,d.entity);
            if (result!=RenderableManager::Builder::Success) return -4;
            auto &transforms=engine.getTransformManager();
            mat4f model;
            std::memcpy(&model[0][0],input.model,sizeof(input.model));
            transforms.setTransform(transforms.getInstance(d.entity),model);
            f.scene->addEntity(d.entity);
        }
        size_t bytes=size_t(desc->width)*desc->height*4;
        f.pixels=static_cast<uint8_t *>(desc->allocate(bytes)); if (!f.pixels) return -2;
        if (!f.renderer->beginFrame(f.swap)) return -4;
        f.renderer->render(f.view);
        f.renderer->readPixels(0,0,desc->width,desc->height,backend::PixelBufferDescriptor(f.pixels,bytes,
            backend::PixelDataFormat::RGBA,backend::PixelDataType::UBYTE,
            [](void *,size_t,void *user) { static_cast<std::atomic<bool> *>(user)->store(true,std::memory_order_release); },&complete));
        f.renderer->endFrame(); engine.flushAndWait();
        if (!complete.load(std::memory_order_acquire)) return -4;
        std::memcpy(out,f.pixels,bytes);
        for (size_t i=3;i<bytes;i+=4) out[i]=255;
        return 0;
    } catch (const std::bad_alloc &) { return -2; }
    catch (...) { return -4; }
}
