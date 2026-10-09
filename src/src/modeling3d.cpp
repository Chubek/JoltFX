#include "jfx/jfx_modeling3d.h"
#include "jfx/jfx_project.h"
#include "tilly/memory.hpp"
#include "tilly/memory.h"
#include "joltscript/compiler.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <xsimd/xsimd.hpp>
#include <CGAL/Simple_cartesian.h>
#include <Eigen/Core>
#include <Eigen/Geometry>
#include <igl/per_face_normals.h>
#include <igl/upsample.h>
#include <vtkMath.h>
#include <btBulletDynamicsCommon.h>
#define TINYPLY_IMPLEMENTATION
#include <tinyply.h>
#define STBIW_MALLOC(n) tilly_mem_alloc(n)
#define STBIW_REALLOC(p,n) tilly_mem_realloc(p,n)
#define STBIW_FREE(p) tilly_mem_free(p)
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <array>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <cstdio>
#include <cstring>
#include <cerrno>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {
using Vec = std::array<float,3>;
using Tri = std::array<uint32_t,3>;
using Transform = std::array<float,9>;
using Input = std::basic_istringstream<char,std::char_traits<char>,tilly::allocator<char>>;
using Output = std::basic_ostringstream<char,std::char_traits<char>,tilly::allocator<char>>;
constexpr double pi=3.14159265358979323846;
constexpr size_t history_limit=32u*1024u*1024u;
struct Key { uint32_t channel,frame,interpolation; float value; };
struct Script { tilly::string source; tilly::vector<uint8_t> code; };
struct Object {
    tilly::string name="Mesh";
    tilly::vector<Vec> vertices;
    tilly::vector<Tri> triangles;
    tilly::vector<Key> keys;
    Transform transform={0,0,0,0,0,0,1,1,1};
    Vec color={.32f,.65f,.9f};
    float mass=0;
    bool visible=true;
    bool smooth=false;
    uint32_t generator=0,resolution=32;
    tilly::vector<std::array<float,4>> controls,balls;
    uint32_t cloner_mode=0,instances=1;
    float spacing=3;
    std::array<Script,9> scripts;
    // Derived immutable geometry data is shared by document/history candidates.
    using Normals=tilly::vector<std::array<glm::vec3,3>>;
    mutable std::shared_ptr<const Normals> normals;
};
struct Document {
    tilly::vector<Object> objects;
    std::array<float,7> camera={35,22,7,0,0,0,45};
    glm::quat orientation=glm::angleAxis(glm::radians(35.f),glm::vec3(0,1,0))*glm::angleAxis(glm::radians(-22.f),glm::vec3(1,0,0));
    uint32_t fps=30,frames=120;
};
void require(bool condition) { if (!condition) throw std::invalid_argument("Invalid 3D command or document"); }
bool number(double v) { return std::isfinite(v) && std::abs(v)<=100000; }
bool channel_value(uint32_t channel,double v) { return channel<9 && number(v) && (channel<6 || v>=.001); }
#include "modeling3d_generators.inc"
Eigen::Vector3f eigen(const Vec &v) { return {v[0],v[1],v[2]}; }
Vec vec(const Eigen::Vector3f &v) { return {v.x(),v.y(),v.z()}; }
glm::quat rotation(const Transform &t) {
    return glm::angleAxis(glm::radians(t[5]),glm::vec3(0,0,1))*
        glm::angleAxis(glm::radians(t[4]),glm::vec3(0,1,0))*glm::angleAxis(glm::radians(t[3]),glm::vec3(1,0,0));
}
void validate_mesh(const Object &o) {
    require(!o.vertices.empty() && o.vertices.size()<=JFX_3D_MAX_VERTICES &&
        !o.triangles.empty() && o.triangles.size()<=JFX_3D_MAX_TRIANGLES);
    for (auto &v:o.vertices) for (float f:v) require(number(double(f)));
    using Kernel=CGAL::Simple_cartesian<double>;
    for (auto &f:o.triangles) {
        for (auto i:f) require(i<o.vertices.size());
        auto point=[&](uint32_t i) { auto &v=o.vertices[i]; return Kernel::Point_3(double(v[0]),double(v[1]),double(v[2])); };
        require(!Kernel::Triangle_3(point(f[0]),point(f[1]),point(f[2])).is_degenerate());
    }
}
void validate(const Document &d) {
    require(d.objects.size()<=JFX_3D_MAX_OBJECTS && d.fps>=1 && d.fps<=240 && d.frames>=1 && d.frames<=36000);
    for (float f:d.camera) require(number(double(f)));
    require(d.camera[1]>=-89 && d.camera[1]<=89 && d.camera[2]>=.1f && d.camera[2]<=10000 && d.camera[6]>=10 && d.camera[6]<=120);
    require(std::isfinite(glm::dot(d.orientation,d.orientation)) && std::abs(glm::dot(d.orientation,d.orientation)-1.f)<.0001f);
    size_t bytes=0;
    size_t render_triangles=0;
    for (auto &o:d.objects) {
        validate_mesh(o); require(o.name.size()<=127 && o.mass>=0 && o.mass<=10000 && o.keys.size()<=JFX_3D_MAX_KEYS);
        for (char ch:o.name) require(static_cast<unsigned char>(ch)>=32);
        for (unsigned i=0;i<9;++i) require(channel_value(i,double(o.transform[i])));
        for (float f:o.color) require(f>=0 && f<=1);
        require(o.generator<=2 && o.resolution>=8 && o.resolution<=64 && o.controls.size()<=16 && o.balls.size()<=16);
        require(o.generator!=1 || o.controls.size()==16);
        require(o.generator!=2 || !o.balls.empty());
        for (auto &points:{&o.controls,&o.balls}) for (auto &p:*points) {
            for (float f:p) require(number(double(f)));
            require(p[3]>=.01f && p[3]<=100);
        }
        require(o.cloner_mode<=3 && o.instances>=1 && o.instances<=64 && (o.cloner_mode || o.instances==1) && o.spacing>=.01f && o.spacing<=1000);
        render_triangles+=o.triangles.size()*o.instances; require(render_triangles<=2u*1024u*1024u);
        for (auto &script:o.scripts) { require(script.source.size()<=4096); bytes+=script.source.size()+script.code.size(); }
        uint64_t last=0; bool first=true;
        for (auto &k:o.keys) {
            require(channel_value(k.channel,double(k.value)) && k.frame<d.frames && k.interpolation<=2);
            uint64_t order=uint64_t(k.channel)*36000+k.frame;
            require(first || order>last); last=order; first=false;
        }
        bytes+=o.vertices.size()*sizeof(Vec)+o.triangles.size()*sizeof(Tri)+o.keys.size()*sizeof(Key);
    }
    require(bytes<=16u*1024u*1024u);
}
Transform sample(const Object &o,double frame) {
    Transform result=o.transform;
    for (uint32_t ch=0;ch<9;++ch) {
        const Key *before=nullptr,*after=nullptr;
        for (auto &k:o.keys) if (k.channel==ch) {
            if (k.frame<=frame) before=&k;
            else { after=&k; break; }
        }
        if (!before && after) result[ch]=after->value;
        else if (before) {
            result[ch]=before->value;
            if (after && before->interpolation) {
                float t=float((frame-before->frame)/(after->frame-before->frame));
                if (before->interpolation==2) t=t*t*(3-2*t);
                result[ch]+=t*(after->value-before->value);
            }
        }
    }
    return result;
}
Transform sample_instance(const Object &o,uint32_t instance,double seconds,uint32_t fps) {
    require(instance<o.instances);
    Transform t=sample(o,seconds*fps); glm::vec3 offset(0);
    if (o.cloner_mode==1) offset.x=float(instance)*o.spacing;
    else if (o.cloner_mode==2) {
        float angle=float(2*pi*instance/o.instances); offset={o.spacing*std::cos(angle),0,o.spacing*std::sin(angle)};
    } else if (o.cloner_mode==3) {
        uint32_t columns=uint32_t(std::ceil(std::sqrt(double(o.instances))));
        offset={float(instance%columns)*o.spacing,0,float(instance/columns)*o.spacing};
    }
    glm::vec3 world=rotation(t)*offset;
    for (unsigned i=0;i<3;++i) t[i]+=world[int(i)];
    bool scripted=false; for (auto &script:o.scripts) scripted|=!script.code.empty();
    if (scripted) {
        jolt_vm_t *vm=jolt_vm_create(); if (!vm) throw std::bad_alloc();
        struct Owner { jolt_vm_t *vm; ~Owner() { jolt_vm_destroy(vm); } } owner{vm};
        for (uint32_t ch=0;ch<9;++ch) if (!o.scripts[ch].code.empty()) {
            auto &code=o.scripts[ch].code; float inputs[4]={float(seconds),float(seconds*fps),float(instance),t[ch]},value=0;
            uint32_t count=0; for (unsigned i=0;i<4;++i) count|=uint32_t(code[8+i])<<(8*i);
            require(jolt_vm_run(vm,code.data(),code.size(),inputs,count,&value,1)==JOLT_OK && channel_value(ch,double(value))); t[ch]=value;
        }
    }
    for (unsigned i=0;i<9;++i) require(channel_value(i,double(t[i])));
    return t;
}
void put_key(Object &o,uint32_t channel,uint32_t frame,float value,uint32_t interpolation) {
    require(channel_value(channel,double(value)) && interpolation<=2);
    auto at=std::lower_bound(o.keys.begin(),o.keys.end(),std::pair<uint32_t,uint32_t>{channel,frame},
        [](const Key &k,const auto &p) { return std::pair<uint32_t,uint32_t>{k.channel,k.frame}<p; });
    if (at!=o.keys.end() && at->channel==channel && at->frame==frame) *at={channel,frame,interpolation,value};
    else { require(o.keys.size()<JFX_3D_MAX_KEYS); o.keys.insert(at,{channel,frame,interpolation,value}); }
}
Object primitive(const char *kind,uint32_t segments=64) {
    require(segments>=8 && segments<=128);
    Object o; o.name=kind;
    if (!std::strcmp(kind,"cube")) {
        o.vertices={{{-1,-1,-1}},{{1,-1,-1}},{{1,1,-1}},{{-1,1,-1}},{{-1,-1,1}},{{1,-1,1}},{{1,1,1}},{{-1,1,1}}};
        o.triangles={{{0,2,1}},{{0,3,2}},{{4,5,6}},{{4,6,7}},{{0,1,5}},{{0,5,4}},{{3,7,6}},{{3,6,2}},{{0,4,7}},{{0,7,3}},{{1,2,6}},{{1,6,5}}};
    } else if (!std::strcmp(kind,"plane")) {
        o.vertices={{{-2,0,-2}},{{2,0,-2}},{{2,0,2}},{{-2,0,2}}}; o.triangles={{{0,2,1}},{{0,3,2}}};
    } else if (!std::strcmp(kind,"sphere")) {
        o.vertices.push_back({0,1,0});
        o.smooth=true;
        const uint32_t slices=segments,rings=segments/2;
        for (uint32_t r=1;r<rings;++r) for (uint32_t s=0;s<slices;++s) {
            double theta=pi*r/rings,phi=2*pi*s/slices;
            o.vertices.push_back({float(std::sin(theta)*std::cos(phi)),float(std::cos(theta)),float(std::sin(theta)*std::sin(phi))});
        }
        uint32_t bottom=uint32_t(o.vertices.size()); o.vertices.push_back({0,-1,0});
        for (uint32_t s=0;s<slices;++s) {
            uint32_t next=(s+1)%slices;
            o.triangles.push_back({0,1+next,1+s});
            o.triangles.push_back({bottom,1+(rings-2)*slices+s,1+(rings-2)*slices+next});
            for (uint32_t r=0;r<rings-2;++r) {
                uint32_t a=1+r*slices+s,b=1+r*slices+next,c=a+slices,d=b+slices;
                o.triangles.push_back({a,b,c}); o.triangles.push_back({b,d,c});
            }
        }
    } else if (!std::strcmp(kind,"nurbs")) {
        o.generator=1; o.smooth=true;
        for (unsigned y=0;y<4;++y) for (unsigned x=0;x<4;++x)
            o.controls.push_back({float(x)-1.5f,(x==1 || x==2) && (y==1 || y==2)?.6f:0.f,float(y)-1.5f,1});
        regenerate(o);
    } else if (!std::strcmp(kind,"metaball")) {
        o.generator=2; o.smooth=true; o.balls={{{-.6f,0,0,1}},{{.6f,0,0,1}}}; regenerate(o);
    } else extra_primitive(o,kind,segments);
    return o;
}
void matrices(const Object &o,Eigen::MatrixXd &v,Eigen::MatrixXi &f) {
    v.resize(Eigen::Index(o.vertices.size()),3); f.resize(Eigen::Index(o.triangles.size()),3);
    for (size_t i=0;i<o.vertices.size();++i) for (int j=0;j<3;++j) v(Eigen::Index(i),j)=double(o.vertices[i][size_t(j)]);
    for (size_t i=0;i<o.triangles.size();++i) for (int j=0;j<3;++j) f(Eigen::Index(i),j)=int(o.triangles[i][size_t(j)]);
}
void subdivide(Object &o) {
    require(o.triangles.size()*4<=JFX_3D_MAX_TRIANGLES && o.vertices.size()+o.triangles.size()*3<=JFX_3D_MAX_VERTICES);
    // libigl's triangle adjacency requires at most two faces on an edge.
    tilly::vector<std::pair<uint32_t,uint32_t>> edges;
    edges.reserve(o.triangles.size()*3);
    for (auto &face:o.triangles) for (size_t j=0;j<3;++j) {
        uint32_t a=face[j],b=face[(j+1)%3]; edges.emplace_back(std::min(a,b),std::max(a,b));
    }
    std::sort(edges.begin(),edges.end());
    for (size_t i=2;i<edges.size();++i) require(edges[i]!=edges[i-2]);
    Eigen::MatrixXd v,subv; Eigen::MatrixXi f,subf; matrices(o,v,f);
    igl::upsample(v,f,subv,subf,1);
    require(subv.rows()<=JFX_3D_MAX_VERTICES);
    o.vertices.resize(size_t(subv.rows())); o.triangles.resize(size_t(subf.rows()));
    for (Eigen::Index i=0;i<subv.rows();++i) for (int j=0;j<3;++j) o.vertices[size_t(i)][size_t(j)]=float(subv(i,j));
    for (Eigen::Index i=0;i<subf.rows();++i) for (int j=0;j<3;++j) o.triangles[size_t(i)][size_t(j)]=uint32_t(subf(i,j));
}
void align_mesh(Object &o) {
    Eigen::Vector3d center=Eigen::Vector3d::Zero();
    for (auto &v:o.vertices) center+=eigen(v).cast<double>();
    center/=double(o.vertices.size());
    double covariance[3][3]={{0}},axes[3][3],values[3];
    for (auto &v:o.vertices) { Eigen::Vector3d delta=eigen(v).cast<double>()-center;
        for (int i=0;i<3;++i) for (int j=0;j<3;++j) covariance[i][j]+=delta[i]*delta[j]; }
    double *rows[3]={covariance[0],covariance[1],covariance[2]},*vectors[3]={axes[0],axes[1],axes[2]};
    require(vtkMath::Jacobi(rows,values,vectors)!=0);
    Eigen::Matrix3d basis;
    for (int i=0;i<3;++i) for (int j=0;j<3;++j) basis(i,j)=axes[i][j];
    if (basis.determinant()<0) basis.col(2)*=-1;
    for (auto &v:o.vertices) v=vec((basis.transpose()*(eigen(v).cast<double>()-center)).cast<float>());
}
tilly::string serialize(const Document &d) {
    Output out; out.exceptions(std::ios::badbit|std::ios::failbit); out.imbue(std::locale::classic()); out<<std::setprecision(9);
    out<<"scene3d 1\nclock "<<d.fps<<' '<<d.frames<<"\ncamera";
    for (float f:d.camera) out<<' '<<f;
    out<<"\nquaternion "<<d.orientation.x<<' '<<d.orientation.y<<' '<<d.orientation.z<<' '<<d.orientation.w<<'\n';
    for (auto &o:d.objects) {
        out<<"object "<<std::quoted(o.name)<<' '<<o.visible<<' '<<o.mass<<'\n'<<"transform";
        for (float f:o.transform) out<<' '<<f;
        out<<"\ncolor";
        for (float f:o.color) out<<' '<<f;
        out<<"\nsmooth "<<o.smooth<<"\ngenerator "<<o.generator<<' '<<o.resolution<<'\n';
        for (auto &p:o.controls) out<<"control "<<p[0]<<' '<<p[1]<<' '<<p[2]<<' '<<p[3]<<'\n';
        for (auto &p:o.balls) out<<"ball "<<p[0]<<' '<<p[1]<<' '<<p[2]<<' '<<p[3]<<'\n';
        out<<"cloner "<<o.cloner_mode<<' '<<o.instances<<' '<<o.spacing<<'\n';
        if (!o.generator) {
            for (auto &v:o.vertices) out<<"v "<<v[0]<<' '<<v[1]<<' '<<v[2]<<'\n';
            for (auto &t:o.triangles) out<<"f "<<t[0]<<' '<<t[1]<<' '<<t[2]<<'\n';
        }
        for (unsigned ch=0;ch<9;++ch) if (!o.scripts[ch].source.empty()) {
            out<<"script "<<ch<<' '; const char *hex="0123456789abcdef";
            for (char character:o.scripts[ch].source) { auto c=static_cast<unsigned char>(character); out<<hex[c>>4]<<hex[c&15]; }
            out<<'\n';
        }
        for (auto &k:o.keys) out<<"key "<<k.channel<<' '<<k.frame<<' '<<k.value<<' '<<k.interpolation<<'\n';
        out<<"end\n";
    }
    auto result=out.str(); require(result.size()<JFX_PROJECT_MAX_BYTES); return result;
}
Document parse(const char *text,size_t length) {
    require(length>0 && length<=JFX_PROJECT_MAX_BYTES && !std::memchr(text,0,length));
    Input input(tilly::string(text,length)); input.imbue(std::locale::classic());
    tilly::string line,word; Document d; Object *object=nullptr; bool header=false,clock=false,camera=false,quaternion=false;
    while (std::getline(input,line)) {
        require(line.size()<16384); Input in(line); in.imbue(std::locale::classic());
        if (!(in>>word) || word[0]=='#') continue;
        if (!header) { unsigned version=0; require(word=="scene3d" && bool(in>>version) && version==1); header=true; }
        else if (word=="clock") { require(!clock && !object && bool(in>>d.fps>>d.frames)); clock=true; }
        else if (word=="camera") { require(!camera && !object); for (float &f:d.camera) require(bool(in>>f)); camera=true; }
        else if (word=="quaternion") {
            require(!quaternion && !object && bool(in>>d.orientation.x>>d.orientation.y>>d.orientation.z>>d.orientation.w)); quaternion=true;
        }
        else if (word=="object") {
            require(!object && d.objects.size()<JFX_3D_MAX_OBJECTS); d.objects.emplace_back(); object=&d.objects.back();
            unsigned visible=0; require(bool(in>>std::quoted(object->name)>>visible>>object->mass) && visible<=1); object->visible=visible!=0;
        } else if (word=="end") {
            require(object!=nullptr);
            if (object->generator) { require(object->vertices.empty() && object->triangles.empty()); regenerate(*object); }
            validate_mesh(*object); object=nullptr;
        }
        else {
            require(object!=nullptr);
            if (word=="transform") { for (float &f:object->transform) require(bool(in>>f)); }
            else if (word=="color") { for (float &f:object->color) require(bool(in>>f)); }
            else if (word=="smooth") { unsigned smooth=0; require(bool(in>>smooth) && smooth<=1); object->smooth=smooth!=0; }
            else if (word=="generator") { require(bool(in>>object->generator>>object->resolution) && object->generator<=2); }
            else if (word=="control" || word=="ball") {
                auto &points=word=="control"?object->controls:object->balls; require(points.size()<16);
                std::array<float,4> p; for (float &f:p) require(bool(in>>f) && number(double(f))); require(p[3]>=.01f && p[3]<=100); points.push_back(p);
            }
            else if (word=="cloner") { require(bool(in>>object->cloner_mode>>object->instances>>object->spacing)); }
            else if (word=="script") {
                unsigned ch=0; tilly::string hex,source; require(bool(in>>ch>>hex) && ch<9 && object->scripts[ch].source.empty() && hex.size()<=8192 && hex.size()%2==0);
                auto digit=[](char c)->unsigned { require((c>='0' && c<='9') || (c>='a' && c<='f')); return unsigned(c<='9'?c-'0':c-'a'+10); };
                for (size_t i=0;i<hex.size();i+=2) { char c=char(digit(hex[i])*16+digit(hex[i+1])); require(c!=0); source+=c; }
                compile_script(object->scripts[ch],source.c_str());
            }
            else if (word=="v") { Vec v; require(object->vertices.size()<JFX_3D_MAX_VERTICES); for (float &f:v) require(bool(in>>f)); object->vertices.push_back(v); }
            else if (word=="f") { Tri t; require(object->triangles.size()<JFX_3D_MAX_TRIANGLES); for (uint32_t &f:t) require(bool(in>>f)); object->triangles.push_back(t); }
            else if (word=="key") { Key k; require(object->keys.size()<JFX_3D_MAX_KEYS && bool(in>>k.channel>>k.frame>>k.value>>k.interpolation)); object->keys.push_back(k); }
            else require(false);
        }
        require(!(in>>word));
    }
    require(header && !object && clock && camera); if (!quaternion) d.orientation=legacy_camera(d.camera); validate(d); return d;
}
Object import_ply(const char *path) {
    std::ifstream in(path,std::ios::binary); require(bool(in));
    in.seekg(0,std::ios::end); auto size=in.tellg(); require(size>0 && size<=16*1024*1024); in.seekg(0);
    tilly::string line; size_t header_bytes=0; bool end=false;
    while (std::getline(in,line)) {
        header_bytes+=line.size()+1; require(header_bytes<=65536 && line.size()<=1024);
        if (line=="end_header" || line=="end_header\r") { end=true; break; }
    }
    require(end); in.clear(); in.seekg(0);
    tinyply::PlyFile ply; require(ply.parse_header(in));
    size_t elements=0;
    for (auto &e:ply.get_elements()) {
        require(e.size<=JFX_3D_MAX_TRIANGLES && e.properties.size()<=32);
        elements+=e.size; require(elements<=JFX_3D_MAX_TRIANGLES*2u);
        for (auto &p:e.properties) {
            require(p.propertyType!=tinyply::Type::INVALID);
            if (p.isList) require(e.name=="face" && p.name=="vertex_indices" &&
                (p.listType==tinyply::Type::UINT8 || p.listType==tinyply::Type::UINT32 || p.listType==tinyply::Type::INT32));
        }
    }
    auto vertices=ply.request_properties_from_element("vertex",{"x","y","z"});
    auto faces=ply.request_properties_from_element("face",{"vertex_indices"},3);
    require(vertices->count>0 && vertices->count<=JFX_3D_MAX_VERTICES && faces->count>0 && faces->count<=JFX_3D_MAX_TRIANGLES);
    require(vertices->t==tinyply::Type::FLOAT32 || vertices->t==tinyply::Type::FLOAT64);
    require(faces->t==tinyply::Type::INT32 || faces->t==tinyply::Type::UINT32);
    ply.read(in); require(!in.fail());
    Object o; o.name="Imported PLY"; o.vertices.resize(vertices->count); o.triangles.resize(faces->count);
    require(faces->buffer.size_bytes()==faces->count*12);
    for (size_t i=0;i<vertices->count;++i) for (size_t j=0;j<3;++j) {
        size_t index=i*3+j;
        if (vertices->t==tinyply::Type::FLOAT32) std::memcpy(&o.vertices[i][j],vertices->buffer.get()+index*4,4);
        else { double v; std::memcpy(&v,vertices->buffer.get()+index*8,8); require(number(v)); o.vertices[i][j]=float(v); }
    }
    std::memcpy(o.triangles.data(),faces->buffer.get(),faces->count*12); validate_mesh(o); return o;
}
// Own only an exclusively created sibling; cleanup also runs on exceptions.
// Match the shared export job's replacement behavior on Windows.
struct ExportFile {
    tilly::string temporary;
    FILE *file=nullptr;
    explicit ExportFile(const char *path) {
        for (unsigned slot=0;slot<1000;++slot) {
            char suffix[32]; std::snprintf(suffix,sizeof(suffix),".jfx-part-%u",slot);
            temporary=tilly::string(path)+suffix;
            errno=0; file=std::fopen(temporary.c_str(),"wbx");
            if (file) return;
            if (errno!=EEXIST) break;
        }
        temporary.clear();
    }
    ~ExportFile() { if (file) std::fclose(file); if (!temporary.empty()) std::remove(temporary.c_str()); }
    ExportFile(const ExportFile &)=delete;
    ExportFile &operator=(const ExportFile &)=delete;
    bool commit(const char *path) {
        if (!file) return false;
        bool ok=!std::ferror(file); if (std::fclose(file)!=0) ok=false; file=nullptr;
        if (!ok) return false;
#ifdef _WIN32
        ok=MoveFileExA(temporary.c_str(),path,MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
#else
        ok=std::rename(temporary.c_str(),path)==0;
#endif
        if (ok) temporary.clear();
        return ok;
    }
};
bool export_ply(const Object &o,const char *path) {
    ExportFile output(path); if (!output.file) return false;
    Output out; out.exceptions(std::ios::badbit|std::ios::failbit);
    tinyply::PlyFile ply;
    ply.add_properties_to_element("vertex",{"x","y","z"},tinyply::Type::FLOAT32,o.vertices.size(),
        reinterpret_cast<const uint8_t *>(o.vertices.data()),tinyply::Type::INVALID,0);
    ply.add_properties_to_element("face",{"vertex_indices"},tinyply::Type::UINT32,o.triangles.size(),
        reinterpret_cast<const uint8_t *>(o.triangles.data()),tinyply::Type::UINT8,3);
    ply.write(out,true); auto bytes=out.str();
    return std::fwrite(bytes.data(),1,bytes.size(),output.file)==bytes.size() && output.commit(path);
}
void bake(Document &d,uint32_t frames) {
    require(frames>=1 && frames<d.frames && frames<=600 && (frames+1)*6<=JFX_3D_MAX_KEYS);
    for (auto &o:d.objects) require(o.instances==1);
    btDefaultCollisionConfiguration config; btCollisionDispatcher dispatcher(&config);
    btDbvtBroadphase broadphase; btSequentialImpulseConstraintSolver solver;
    btDiscreteDynamicsWorld world(&dispatcher,&broadphase,&solver,&config); world.setGravity({0,-9.81f,0});
    // Fixed-size owner storage avoids allocation after objects enter the world.
    tilly::vector<tilly::unique_ptr<btConvexHullShape>> shapes;
    tilly::vector<tilly::unique_ptr<btRigidBody>> bodies;
    shapes.reserve(d.objects.size()); bodies.reserve(d.objects.size());
    struct Cleanup { btDiscreteDynamicsWorld &world; tilly::vector<tilly::unique_ptr<btRigidBody>> &bodies;
        ~Cleanup() { for (auto &b:bodies) world.removeRigidBody(b.get()); } } cleanup{world,bodies};
    for (auto &o:d.objects) {
        tilly::unique_ptr<btConvexHullShape> shape(tilly::create<btConvexHullShape>()); if (!shape) throw std::bad_alloc();
        for (auto &v:o.vertices) shape->addPoint({v[0]*o.transform[6],v[1]*o.transform[7],v[2]*o.transform[8]},false);
        shape->recalcLocalAabb(); btVector3 inertia(0,0,0); if (o.mass>0) shape->calculateLocalInertia(o.mass,inertia);
        btRigidBody::btRigidBodyConstructionInfo info(o.mass,nullptr,shape.get(),inertia);
        tilly::unique_ptr<btRigidBody> body(tilly::create<btRigidBody>(info)); if (!body) throw std::bad_alloc();
        btQuaternion q; q.setEulerZYX(o.transform[5]*float(pi/180),o.transform[4]*float(pi/180),o.transform[3]*float(pi/180));
        body->setWorldTransform(btTransform(q,{o.transform[0],o.transform[1],o.transform[2]}));
        body->setFriction(.5f); world.addRigidBody(body.get());
        shapes.push_back(std::move(shape)); bodies.push_back(std::move(body));
        if (o.mass>0) {
            o.keys.erase(std::remove_if(o.keys.begin(),o.keys.end(),[](auto &k) { return k.channel<6; }),o.keys.end());
            for (unsigned ch=0;ch<6;++ch) o.scripts[ch]=Script{};
        }
    }
    for (uint32_t frame=0;frame<=frames;++frame) {
        if (frame) world.stepSimulation(btScalar(1)/btScalar(d.fps),4,btScalar(1)/(btScalar(d.fps)*4));
        for (size_t i=0;i<d.objects.size();++i) if (d.objects[i].mass>0) {
            auto &o=d.objects[i]; auto &t=bodies[i]->getWorldTransform();
            btScalar yaw,pitch,roll; t.getBasis().getEulerZYX(yaw,pitch,roll);
            float values[6]={t.getOrigin().x(),t.getOrigin().y(),t.getOrigin().z(),roll*float(180/pi),pitch*float(180/pi),yaw*float(180/pi)};
            for (uint32_t ch=0;ch<6;++ch) put_key(o,ch,frame,values[ch],1);
        }
    }
}
jfx_result_t apply(Document &d,const char *op,uint32_t a,uint32_t b,uint32_t c,double value,const char *text) {
    require(number(value));
    if (!std::strcmp(op,"3d.new")) { d=Document{}; return JFX_SUCCESS; }
    if (!std::strcmp(op,"3d.add") || !std::strcmp(op,"3d.import_ply")) {
        require(d.objects.size()<JFX_3D_MAX_OBJECTS && text && *text);
        d.objects.push_back(!std::strcmp(op,"3d.add")?primitive(text,a?a:64):import_ply(text)); return JFX_SUCCESS;
    }
    if (!std::strcmp(op,"3d.camera")) { require(a<7); d.camera[a]=float(value); if (a<2) d.orientation=legacy_camera(d.camera); return JFX_SUCCESS; }
    if (!std::strcmp(op,"3d.orbit")) {
        auto v=glm::radians(read_xyz(text));
        auto delta=glm::angleAxis(v.z,glm::vec3(0,0,1))*glm::angleAxis(v.x,glm::vec3(0,1,0))*glm::angleAxis(v.y,glm::vec3(1,0,0));
        d.orientation=glm::normalize(d.orientation*delta); return JFX_SUCCESS;
    }
    if (!std::strcmp(op,"3d.orbit_axis")) {
        auto axis=read_xyz(text); require(glm::length(axis)>.0001f);
        d.orientation=glm::normalize(glm::angleAxis(glm::radians(float(value)),glm::normalize(axis))*d.orientation); return JFX_SUCCESS;
    }
    if (!std::strcmp(op,"3d.pan")) { auto v=d.orientation*read_xyz(text); for (unsigned i=0;i<3;++i) d.camera[3+i]+=v[int(i)]; return JFX_SUCCESS; }
    if (!std::strcmp(op,"3d.dolly")) { require(std::abs(value)<=10); d.camera[2]=std::clamp(d.camera[2]*float(std::exp(-value)),.1f,10000.f); return JFX_SUCCESS; }
    if (!std::strcmp(op,"3d.view")) {
        require(a<6); static const float angles[6][2]={{0,0},{90,0},{0,90},{180,0},{-90,0},{0,-90}};
        d.orientation=glm::angleAxis(glm::radians(angles[a][0]),glm::vec3(0,1,0))*glm::angleAxis(glm::radians(-angles[a][1]),glm::vec3(1,0,0)); return JFX_SUCCESS;
    }
    if (!std::strcmp(op,"3d.clock")) { require(a>=1 && a<=240 && b>=1 && b<=36000); d.fps=a; d.frames=b; return JFX_SUCCESS; }
    if (!std::strcmp(op,"3d.bake")) { bake(d,c); return JFX_SUCCESS; }
    require(a<d.objects.size()); auto &o=d.objects[a];
    if (!std::strcmp(op,"3d.remove")) d.objects.erase(d.objects.begin()+a);
    else if (!std::strcmp(op,"3d.duplicate")) { require(d.objects.size()<JFX_3D_MAX_OBJECTS); Object copy=o; copy.name+=" copy"; d.objects.push_back(std::move(copy)); }
    else if (!std::strcmp(op,"3d.name")) { require(text && std::strlen(text)<=127); o.name=text; }
    else if (!std::strcmp(op,"3d.transform")) { require(channel_value(b,value)); o.transform[b]=float(value); }
    else if (!std::strcmp(op,"3d.vertex")) { require(!o.generator && b<o.vertices.size() && c<3); o.vertices[b][c]=float(value); }
    else if (!std::strcmp(op,"3d.color")) { require(b<3 && value>=0 && value<=1); o.color[b]=float(value); }
    else if (!std::strcmp(op,"3d.visible")) { require(value==0 || value==1); o.visible=value!=0; }
    else if (!std::strcmp(op,"3d.mass")) { require(value>=0 && value<=10000); o.mass=float(value); }
    else if (!std::strcmp(op,"3d.smooth")) { require(value==0 || value==1); o.smooth=value!=0; }
    else if (!std::strcmp(op,"3d.script") || !std::strcmp(op,"3d.script_file")) {
        require(b<9 && text);
        if (!std::strcmp(op,"3d.script_file")) {
            std::ifstream file(text,std::ios::binary); require(bool(file)); file.seekg(0,std::ios::end); auto length=file.tellg();
            require(length>0 && length<=4096); file.seekg(0); tilly::string source(size_t(length),'\0');
            file.read(source.data(),length); require(bool(file) && !std::memchr(source.data(),0,source.size())); compile_script(o.scripts[b],source.c_str());
        } else compile_script(o.scripts[b],text);
    }
    else if (!std::strcmp(op,"3d.cloner")) { require(b<=3 && c>=1 && c<=64 && value>=.01 && value<=1000); o.cloner_mode=b; o.instances=b?c:1; o.spacing=float(value); }
    else if (!std::strcmp(op,"3d.cloner_make_real")) {
        require(d.objects.size()+o.instances-1<=JFX_3D_MAX_OBJECTS && value>=0);
        tilly::vector<Object> copies; copies.reserve(o.instances);
        for (uint32_t i=0;i<o.instances;++i) {
            Object copy=o; copy.transform=sample_instance(o,i,value,d.fps); copy.cloner_mode=0; copy.instances=1;
            copy.keys.clear(); for (auto &script:copy.scripts) script=Script{}; copies.push_back(std::move(copy));
        }
        d.objects.erase(d.objects.begin()+a); d.objects.insert(d.objects.begin()+a,copies.begin(),copies.end());
    }
    else if (!std::strcmp(op,"3d.nurbs_point") || !std::strcmp(op,"3d.metaball_point")) {
        bool nurbs=!std::strcmp(op,"3d.nurbs_point"); require(o.generator==(nurbs?1u:2u)); auto &points=nurbs?o.controls:o.balls;
        require(b<points.size() && c<4 && (c<3 || (value>=.01 && value<=100))); points[b][c]=float(value); regenerate(o);
    }
    else if (!std::strcmp(op,"3d.resolution")) { require(o.generator && b>=8 && b<=64); o.resolution=b; regenerate(o); }
    else if (!std::strcmp(op,"3d.metaball_add")) {
        require(o.generator==2 && o.balls.size()<16 && text); Input in(text); in.imbue(std::locale::classic());
        std::array<float,4> p; for (float &f:p) require(bool(in>>f) && number(double(f))); tilly::string extra; require(!(in>>extra) && p[3]>=.01f && p[3]<=100);
        o.balls.push_back(p); regenerate(o);
    }
    else if (!std::strcmp(op,"3d.metaball_remove")) { require(o.generator==2 && b<o.balls.size() && o.balls.size()>1); o.balls.erase(o.balls.begin()+b); regenerate(o); }
    else if (!std::strcmp(op,"3d.make_editable")) { o.generator=0; o.controls.clear(); o.balls.clear(); }
    else if (!std::strcmp(op,"3d.key")) { require(c<d.frames); put_key(o,b,c,float(value),1); }
    else if (!std::strcmp(op,"3d.key_remove") || !std::strcmp(op,"3d.interpolation")) {
        auto at=std::find_if(o.keys.begin(),o.keys.end(),[&](auto &k) { return k.channel==b && k.frame==c; }); require(at!=o.keys.end());
        if (!std::strcmp(op,"3d.key_remove")) o.keys.erase(at);
        else { require(value>=0 && value<=2 && std::floor(value)==value); at->interpolation=uint32_t(value); }
    }
    else if (!std::strcmp(op,"3d.subdivide")) { require(!o.generator); subdivide(o); }
    else if (!std::strcmp(op,"3d.align")) { require(!o.generator); align_mesh(o); }
    else if (!std::strcmp(op,"3d.export_ply")) { require(text && *text); return export_ply(o,text)?JFX_SUCCESS:JFX_ERROR_BACKEND_FAILURE; }
    else require(false);
    return JFX_SUCCESS;
}
tilly::string json_string(const tilly::string &text) {
    tilly::string out="\"";
    for (char character:text) {
        auto c=static_cast<unsigned char>(character);
        if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
        else if (c<32) { char buf[7]; std::snprintf(buf,sizeof(buf),"\\u%04x",unsigned(c)); out+=buf; }
        else out+=char(c);
    }
    out+='"'; return out;
}
jfx_result_t copy_text(const tilly::string &text,char *out,size_t capacity,size_t *written=nullptr) {
    if (text.size()+1>capacity) return JFX_ERROR_OUT_OF_MEMORY;
    std::memcpy(out,text.c_str(),text.size()+1); if (written) *written=text.size(); return JFX_SUCCESS;
}
template<class Fn> jfx_result_t boundary(Fn fn) {
    try { return fn(); } catch (const std::bad_alloc &) { return JFX_ERROR_OUT_OF_MEMORY; }
    catch (...) { return JFX_ERROR_INVALID_ARGUMENT; }
}
#include "modeling3d_renderer.inc"
} // namespace

struct jfx_scene3d {
    Document document;
    tilly::vector<tilly::string> undo,redo;
    size_t history_bytes=0;
    bool navigating=false;
    std::array<float,7> navigation_camera;
    glm::quat navigation_orientation;
};
extern "C" jfx_scene3d_t *jfx_scene3d_create() {
    // Bullet is private to this engine; install its process-lifetime hooks once.
    static const bool configured=[] {
        btAlignedAllocSetCustom([](size_t n)->void * { void *p=tilly_mem_alloc(n); if (!p) throw std::bad_alloc(); return p; },tilly_mem_free);
        btAlignedAllocSetCustomAligned([](size_t n,int alignment)->void * {
            void *p=tilly_alloc((tilly_allocator_t *)tilly_default_allocator(),n,size_t(alignment));
            if (!p) throw std::bad_alloc();
            return p;
        },tilly_mem_free); return true;
    }();
    (void)configured; return tilly::create<jfx_scene3d>();
}
extern "C" void jfx_scene3d_destroy(jfx_scene3d_t *s) { tilly::destroy(s); }
extern "C" uint32_t jfx_scene3d_object_count(const jfx_scene3d_t *s) { return s?uint32_t(s->document.objects.size()):0; }
extern "C" uint32_t jfx_scene3d_fps(const jfx_scene3d_t *s) { return s?s->document.fps:0; }
extern "C" uint32_t jfx_scene3d_frames(const jfx_scene3d_t *s) { return s?s->document.frames:0; }
extern "C" jfx_result_t jfx_scene3d_object_info(const jfx_scene3d_t *s,uint32_t index,jfx_object3d_info_t *out) {
    if (!s || !out || out->size<sizeof(*out) || index>=s->document.objects.size()) return JFX_ERROR_INVALID_ARGUMENT;
    auto &o=s->document.objects[index]; jfx_object3d_info_t info={}; info.size=sizeof(info);
    std::snprintf(info.name,sizeof(info.name),"%s",o.name.c_str()); info.vertices=uint32_t(o.vertices.size()); info.triangles=uint32_t(o.triangles.size()); info.keys=uint32_t(o.keys.size());
    std::copy(o.transform.begin(),o.transform.end(),info.transform); std::copy(o.color.begin(),o.color.end(),info.color);
    info.mass=o.mass; info.visible=o.visible; *out=info; return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_scene3d_vertex(const jfx_scene3d_t *s,uint32_t object,uint32_t vertex,float out[3]) {
    if (!s || !out || object>=s->document.objects.size() || vertex>=s->document.objects[object].vertices.size()) return JFX_ERROR_INVALID_ARGUMENT;
    auto &v=s->document.objects[object].vertices[vertex]; std::copy(v.begin(),v.end(),out); return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_scene3d_camera(const jfx_scene3d_t *s,float out[7]) {
    if (!s || !out) return JFX_ERROR_INVALID_ARGUMENT;
    std::copy(s->document.camera.begin(),s->document.camera.end(),out); return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_scene3d_camera_quaternion(const jfx_scene3d_t *s,float out[4]) {
    if (!s || !out) return JFX_ERROR_INVALID_ARGUMENT;
    auto &q=s->document.orientation; out[0]=q.x; out[1]=q.y; out[2]=q.z; out[3]=q.w; return JFX_SUCCESS;
}
extern "C" jfx_result_t jfx_scene3d_script(const jfx_scene3d_t *s,uint32_t object,uint32_t channel,char *out,size_t capacity) {
    if (!s || !out || object>=s->document.objects.size() || channel>=9) return JFX_ERROR_INVALID_ARGUMENT;
    return copy_text(s->document.objects[object].scripts[channel].source,out,capacity);
}
extern "C" jfx_result_t jfx_scene3d_procedural_info(const jfx_scene3d_t *s,uint32_t index,jfx_procedural3d_info_t *out) {
    if (!s || !out || out->size<sizeof(*out) || index>=s->document.objects.size()) return JFX_ERROR_INVALID_ARGUMENT;
    auto &o=s->document.objects[index]; jfx_procedural3d_info_t info={}; info.size=sizeof(info);
    info.generator=o.generator; info.resolution=o.resolution; info.control_count=uint32_t(o.controls.size()); info.ball_count=uint32_t(o.balls.size());
    for (size_t i=0;i<o.controls.size();++i) std::copy(o.controls[i].begin(),o.controls[i].end(),info.controls[i]);
    for (size_t i=0;i<o.balls.size();++i) std::copy(o.balls[i].begin(),o.balls[i].end(),info.balls[i]);
    info.cloner_mode=o.cloner_mode; info.instances=o.instances; info.spacing=o.spacing; info.smooth=o.smooth; *out=info; return JFX_SUCCESS;
}
extern "C" bool jfx_scene3d_can_undo(const jfx_scene3d_t *s) { return s && !s->undo.empty(); }
extern "C" bool jfx_scene3d_can_redo(const jfx_scene3d_t *s) { return s && !s->redo.empty(); }
extern "C" void jfx_scene3d_clear_history(jfx_scene3d_t *s) {
    if (s) { s->undo.clear(); s->redo.clear(); s->history_bytes=0; }
}
extern "C" jfx_result_t jfx_scene3d_command(jfx_scene3d_t *s,const char *op,uint32_t a,uint32_t b,uint32_t c,double value,const char *text) {
    if (!s || !op || !number(value)) return JFX_ERROR_INVALID_ARGUMENT;
    return boundary([&] {
        if (!std::strcmp(op,"3d.navigation_begin")) {
            require(!s->navigating); s->navigation_camera=s->document.camera; s->navigation_orientation=s->document.orientation; s->navigating=true; return JFX_SUCCESS;
        }
        if (!std::strcmp(op,"3d.navigation_cancel")) {
            require(s->navigating); s->document.camera=s->navigation_camera; s->document.orientation=s->navigation_orientation; s->navigating=false; return JFX_SUCCESS;
        }
        bool finish=!std::strcmp(op,"3d.navigation_end");
        if (s->navigating && !finish) {
            require(!std::strcmp(op,"3d.orbit") || !std::strcmp(op,"3d.orbit_axis") || !std::strcmp(op,"3d.pan") || !std::strcmp(op,"3d.dolly") || !std::strcmp(op,"3d.view") || !std::strcmp(op,"3d.camera"));
            // Camera gestures cannot change geometry: avoid copying/validating
            // every mesh on each mouse event. Validate a camera-only candidate.
            Document next; next.camera=s->document.camera; next.orientation=s->document.orientation;
            auto r=apply(next,op,a,b,c,value,text); if (r!=JFX_SUCCESS) return r; validate(next);
            s->document.camera=next.camera; s->document.orientation=next.orientation; return JFX_SUCCESS;
        }
        bool undo=!std::strcmp(op,"undo"),redo=!std::strcmp(op,"redo");
        if (undo || redo) {
            auto &from=undo?s->undo:s->redo; auto &to=undo?s->redo:s->undo; require(!from.empty());
            Document restored=parse(from.back().data(),from.back().size());
            auto current=serialize(s->document); to.reserve(to.size()+1);
            s->history_bytes-=from.back().size(); from.pop_back(); s->history_bytes+=current.size(); to.push_back(std::move(current));
            s->document=std::move(restored);
        } else {
            Document next=s->document;
            if (a<next.objects.size() && (!std::strcmp(op,"3d.vertex") || !std::strcmp(op,"3d.subdivide") ||
                !std::strcmp(op,"3d.align") || !std::strcmp(op,"3d.smooth") || !std::strcmp(op,"3d.make_editable"))) next.objects[a].normals.reset();
            auto r=finish?JFX_SUCCESS:apply(next,op,a,b,c,value,text); if (r!=JFX_SUCCESS) return r;
            if (!std::strcmp(op,"3d.export_ply")) return r;
            validate(next); tilly::string before;
            if (finish) {
                require(s->navigating); Document old=next; old.camera=s->navigation_camera; old.orientation=s->navigation_orientation; before=serialize(old);
                if (before==serialize(next)) { s->navigating=false; return JFX_SUCCESS; }
            } else before=serialize(s->document);
            (void)serialize(next);
            s->undo.reserve(s->undo.size()+1);
            for (auto &entry:s->redo) s->history_bytes-=entry.size();
            s->redo.clear();
            s->history_bytes+=before.size(); s->undo.push_back(std::move(before)); s->document=std::move(next);
            s->navigating=false;
        }
        while (s->undo.size()>32 || s->redo.size()>32 || s->history_bytes>history_limit) {
            auto &stack=s->undo.empty() || s->redo.size()>32?s->redo:s->undo;
            if (stack.empty()) break;
            s->history_bytes-=stack.front().size(); stack.erase(stack.begin());
        }
        return JFX_SUCCESS;
    });
}
extern "C" jfx_result_t jfx_scene3d_load(jfx_scene3d_t *s,const char *text,size_t length,char *err,size_t cap) {
    if (!s || !text) return JFX_ERROR_INVALID_ARGUMENT;
    auto r=boundary([&] { Document d=parse(text,length); s->document=std::move(d); s->undo.clear(); s->redo.clear(); s->history_bytes=0; s->navigating=false; return JFX_SUCCESS; });
    if (err && cap) std::snprintf(err,cap,"%s",r==JFX_SUCCESS?"":"Invalid or oversized 3D scene (check mesh indices, keys and camera)");
    return r;
}
extern "C" jfx_result_t jfx_scene3d_save(const jfx_scene3d_t *s,char *out,size_t cap,size_t *written) {
    if (!s || !out || !written) return JFX_ERROR_INVALID_ARGUMENT;
    return boundary([&] { return copy_text(serialize(s->document),out,cap,written); });
}
extern "C" jfx_result_t jfx_scene3d_state(const jfx_scene3d_t *s,char *out,size_t cap) {
    if (!s || !out) return JFX_ERROR_INVALID_ARGUMENT;
    return boundary([&] {
        auto &d=s->document; Output json; json.exceptions(std::ios::badbit|std::ios::failbit); json.imbue(std::locale::classic()); json<<std::setprecision(9);
        json<<"{\"fps\":"<<d.fps<<",\"frames\":"<<d.frames<<",\"undo\":"<<(s->undo.empty()?"false":"true")
            <<",\"redo\":"<<(s->redo.empty()?"false":"true")<<",\"camera\":[";
        for (size_t i=0;i<d.camera.size();++i) { if (i) json<<','; json<<d.camera[i]; }
        json<<"],\"quaternion\":["<<d.orientation.x<<','<<d.orientation.y<<','<<d.orientation.z<<','<<d.orientation.w<<"],\"objects\":[";
        for (size_t i=0;i<d.objects.size();++i) {
            if (i) json<<',';
            auto &o=d.objects[i]; json<<"{\"id\":"<<i<<",\"name\":"<<json_string(o.name)
                <<",\"vertices\":"<<o.vertices.size()<<",\"triangles\":"<<o.triangles.size()<<",\"visible\":"<<(o.visible?"true":"false")
                <<",\"mass\":"<<o.mass<<",\"transform\":[";
            for (size_t j=0;j<9;++j) { if (j) json<<','; json<<o.transform[j]; }
            json<<"],\"color\":["<<o.color[0]<<','<<o.color[1]<<','<<o.color[2]<<"],\"keys\":[";
            for (size_t j=0;j<o.keys.size();++j) { if (j) json<<','; auto &k=o.keys[j]; json<<"{\"channel\":"<<k.channel<<",\"frame\":"<<k.frame<<",\"value\":"<<k.value<<",\"interpolation\":"<<k.interpolation<<'}'; }
            json<<"],\"smooth\":"<<(o.smooth?"true":"false")<<",\"generator\":"<<o.generator<<",\"resolution\":"<<o.resolution;
            auto points=[&](const char *name,const auto &list) {
                json<<",\""<<name<<"\":[";
                for (size_t j=0;j<list.size();++j) { if (j) json<<','; auto &p=list[j]; json<<'['<<p[0]<<','<<p[1]<<','<<p[2]<<','<<p[3]<<']'; } json<<']';
            };
            points("controls",o.controls); points("balls",o.balls);
            json<<",\"cloner\":{\"mode\":"<<o.cloner_mode<<",\"count\":"<<o.instances<<",\"spacing\":"<<o.spacing<<"},\"scripts\":[";
            for (size_t ch=0;ch<9;++ch) { if (ch) json<<','; json<<json_string(o.scripts[ch].source); } json<<"]}";
        }
        json<<"]}"; return copy_text(json.str(),out,cap);
    });
}
extern "C" jfx_result_t jfx_scene3d_sample(const jfx_scene3d_t *s,uint32_t object,double seconds,float out[9]) {
    return jfx_scene3d_sample_instance(s,object,0,seconds,out);
}
extern "C" jfx_result_t jfx_scene3d_sample_instance(const jfx_scene3d_t *s,uint32_t object,uint32_t instance,double seconds,float out[9]) {
    if (!s || !out || object>=s->document.objects.size() || !std::isfinite(seconds) || seconds<0 || seconds>1e9) return JFX_ERROR_INVALID_ARGUMENT;
    return boundary([&] { auto t=sample_instance(s->document.objects[object],instance,seconds,s->document.fps); std::copy(t.begin(),t.end(),out); return JFX_SUCCESS; });
}
extern "C" jfx_result_t jfx_scene3d_render(const jfx_scene3d_t *s,double seconds,uint32_t w,uint32_t h,uint8_t *out,size_t cap) {
    if (!s || !out || !w || !h || w>2048 || h>2048 || cap<size_t(w)*h*4 || !std::isfinite(seconds) || seconds<0 || seconds>1e9) return JFX_ERROR_INVALID_ARGUMENT;
    return boundary([&] {
        return render_document(s->document,seconds,w,h,out);
    });
}
extern "C" jfx_result_t jfx_scene3d_write_png(const jfx_scene3d_t *s,double seconds,uint32_t w,uint32_t h,const char *path) {
    if (!s || !path || !*path || !w || !h || w>2048 || h>2048) return JFX_ERROR_INVALID_ARGUMENT;
    return boundary([&] {
        tilly::vector<uint8_t> pixels(size_t(w)*h*4);
        auto r=jfx_scene3d_render(s,seconds,w,h,pixels.data(),pixels.size()); if (r!=JFX_SUCCESS) return r;
        ExportFile output(path); if (!output.file) return JFX_ERROR_BACKEND_FAILURE;
        auto write=[](void *context,void *data,int size) { std::fwrite(data,1,size_t(size),static_cast<FILE *>(context)); };
        bool ok=stbi_write_png_to_func(write,output.file,int(w),int(h),4,pixels.data(),int(w*4))!=0 && output.commit(path);
        return ok?JFX_SUCCESS:JFX_ERROR_BACKEND_FAILURE;
    });
}
