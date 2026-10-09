/* Four coverage samples per output pixel; perspective-correct normals and
 * depth. Clip before projection so navigating through a mesh stays stable. */
struct RenderVertex { glm::vec3 position,normal; };
glm::vec3 unit_normal(const glm::vec3 &n) {
    float length=glm::length(n); return length>1e-20f?n/length:glm::vec3(0,1,0);
}
jfx_result_t render_document(const Document &d,double seconds,uint32_t w,uint32_t h,uint8_t *out) {
    uint32_t sw=w*2,sh=h*2;
    tilly::vector<uint8_t> pixels(size_t(sw)*sh*4);
    tilly::vector<float> depth(size_t(sw)*sh,std::numeric_limits<float>::infinity());
    for (size_t i=0;i<depth.size();++i) { pixels[i*4]=22; pixels[i*4+1]=26; pixels[i*4+2]=34; pixels[i*4+3]=255; }
    auto &c=d.camera; glm::vec3 eye=glm::vec3(c[3],c[4],c[5])+d.orientation*glm::vec3(0,0,c[2]);
    glm::quat view=glm::conjugate(d.orientation); float focal=float(sh)*.5f/std::tan(glm::radians(c[6])*.5f);
    glm::vec3 sun=glm::normalize(glm::vec3(.4f,.8f,.3f));
    for (auto &o:d.objects) if (o.visible) {
        if (!o.normals) {
        tilly::vector<glm::vec3> face_normals(o.triangles.size());
        tilly::vector<tilly::vector<uint32_t>> adjacent(o.vertices.size());
        for (uint32_t i=0;i<o.triangles.size();++i) {
            auto &f=o.triangles[i]; face_normals[i]=glm::cross(gvec(o.vertices[f[1]])-gvec(o.vertices[f[0]]),gvec(o.vertices[f[2]])-gvec(o.vertices[f[0]]));
            if (o.smooth) for (auto v:f) adjacent[v].push_back(i);
        }
        auto cached=std::allocate_shared<Object::Normals>(tilly::allocator<Object::Normals>{},o.triangles.size());
        auto &normals=*cached;
        for (size_t i=0;i<o.triangles.size();++i) for (unsigned j=0;j<3;++j) {
            auto vertex=o.triangles[i][j]; auto n=face_normals[i];
            if (o.smooth) {
                if (o.generator==2) n=ball_normal(o,gvec(o.vertices[vertex]));
                else {
                    n=glm::vec3(0); auto base=unit_normal(face_normals[i]);
                    for (auto face:adjacent[vertex]) if (glm::dot(base,unit_normal(face_normals[face]))>.65f) n+=face_normals[face];
                }
            }
            normals[i][j]=unit_normal(n);
        }
        o.normals=std::move(cached);
        }
        const auto &normals=*o.normals;
        for (uint32_t instance=0;instance<o.instances;++instance) {
            Transform t=sample_instance(o,instance,seconds,d.fps);
            glm::quat rot=rotation(t);
            glm::vec3 scale(t[6],t[7],t[8]),translate(t[0],t[1],t[2]);
            tilly::vector<glm::vec3> positions; positions.reserve(o.vertices.size());
            for (auto &v:o.vertices) {
                glm::vec3 p=view*(rot*(gvec(v)*scale)+translate-eye); p.z=-p.z; positions.push_back(p);
            }
            for (size_t index=0;index<o.triangles.size();++index) {
                std::array<RenderVertex,5> source,clipped;
                for (unsigned j=0;j<3;++j) source[j]={positions[o.triangles[index][j]],unit_normal(rot*(normals[index][j]/scale))};
                unsigned count=0;
                for (unsigned j=0;j<3;++j) {
                    auto a=source[j],b=source[(j+1)%3]; bool ain=a.position.z>=.05f,bin=b.position.z>=.05f;
                    if (ain) clipped[count++]=a;
                    if (ain!=bin) {
                        float alpha=(.05f-a.position.z)/(b.position.z-a.position.z);
                        clipped[count++]={glm::mix(a.position,b.position,alpha),glm::mix(a.normal,b.normal,alpha)};
                    }
                }
                for (unsigned triangle=1;triangle+1<count;++triangle) {
                    RenderVertex v[3]={clipped[0],clipped[triangle],clipped[triangle+1]}; glm::vec3 p[3];
                    for (unsigned j=0;j<3;++j) p[j]={float(sw)*.5f+focal*v[j].position.x/v[j].position.z,float(sh)*.5f-focal*v[j].position.y/v[j].position.z,v[j].position.z};
                    auto edge=[](const glm::vec3 &a,const glm::vec3 &b,float x,float y) { return (x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x); };
                    float area=edge(p[0],p[1],p[2].x,p[2].y); if (std::abs(area)<.0001f) continue;
                    float minx=std::min({p[0].x,p[1].x,p[2].x}),maxx=std::max({p[0].x,p[1].x,p[2].x});
                    float miny=std::min({p[0].y,p[1].y,p[2].y}),maxy=std::max({p[0].y,p[1].y,p[2].y});
                    if (maxx<0 || maxy<0 || minx>=float(sw) || miny>=float(sh)) continue;
                    int x0=int(std::max(0.f,minx)),x1=int(std::min(float(sw-1),maxx)),y0=int(std::max(0.f,miny)),y1=int(std::min(float(sh-1),maxy));
                    for (int y=y0;y<=y1;++y) {
                        int x=x0;
#if defined(NDEBUG) && !defined(JFX_CPU_SIMD_OFF) && !defined(XSIMD_NO_SUPPORTED_ARCHITECTURE)
                        using B=xsimd::batch<float>;
                        alignas(64) float lanes[B::size],zs[B::size],lights[B::size];
                        for (size_t lane=0;lane<B::size;++lane) lanes[lane]=float(lane)+.5f;
                        const auto offsets=B::load_unaligned(lanes);
                        for (;x1-x+1>=int(B::size);x+=int(B::size)) {
                            auto xx=B(float(x))+offsets,yy=B(float(y)+.5f);
                            auto edge_batch=[&](const glm::vec3 &a,const glm::vec3 &b) { return (xx-B(a.x))*B(b.y-a.y)-(yy-B(a.y))*B(b.x-a.x); };
                            auto wa=edge_batch(p[1],p[2])/B(area),wb=edge_batch(p[2],p[0])/B(area),wc=B(1)-wa-wb;
                            auto covered=(wa>=B(0)) & (wb>=B(0)) & (wc>=B(0));
                            if (!xsimd::any(covered)) continue;
                            wa/=B(p[0].z); wb/=B(p[1].z); wc/=B(p[2].z);
                            auto z=B(1)/(wa+wb+wc); size_t pixel=size_t(y)*sw+size_t(x);
                            covered=covered & (z<B::load_unaligned(depth.data()+pixel));
                            if (!xsimd::any(covered)) continue;
                            auto nx=B(v[0].normal.x)*wa+B(v[1].normal.x)*wb+B(v[2].normal.x)*wc;
                            auto ny=B(v[0].normal.y)*wa+B(v[1].normal.y)*wb+B(v[2].normal.y)*wc;
                            auto nz=B(v[0].normal.z)*wa+B(v[1].normal.z)*wb+B(v[2].normal.z)*wc;
                            auto length=xsimd::sqrt(nx*nx+ny*ny+nz*nz);
                            auto safe=xsimd::max(length,B(1e-20f));
                            auto dot=(nx/safe)*B(sun.x)+(ny/safe)*B(sun.y)+(nz/safe)*B(sun.z);
                            dot=xsimd::select(length>B(1e-20f),dot,B(sun.y));
                            (B(.25f)+B(.75f)*xsimd::abs(dot)).store_unaligned(lights); z.store_unaligned(zs);
                            for (size_t lane=0;lane<B::size;++lane) if (covered.get(lane)) {
                                depth[pixel+lane]=zs[lane];
                                for (size_t ch=0;ch<3;++ch) pixels[(pixel+lane)*4+ch]=uint8_t(std::clamp(o.color[ch]*lights[lane]*255.f,0.f,255.f));
                            }
                        }
#endif
                        for (;x<=x1;++x) {
                        float wa=edge(p[1],p[2],float(x)+.5f,float(y)+.5f)/area,wb=edge(p[2],p[0],float(x)+.5f,float(y)+.5f)/area,wc=1-wa-wb;
                        if (wa<0 || wb<0 || wc<0) continue;
                        wa/=p[0].z; wb/=p[1].z; wc/=p[2].z; float z=1/(wa+wb+wc); size_t pixel=size_t(y)*sw+size_t(x);
                        if (z>=depth[pixel]) continue;
                        depth[pixel]=z; glm::vec3 n=unit_normal(v[0].normal*wa+v[1].normal*wb+v[2].normal*wc);
                        float light=.25f+.75f*std::abs(glm::dot(n,sun));
                        for (size_t ch=0;ch<3;++ch) pixels[pixel*4+ch]=uint8_t(std::clamp(o.color[ch]*light*255.f,0.f,255.f));
                        }
                    }
                }
            }
        }
    }
    // Publish only after every instance's script and geometry has succeeded.
    for (uint32_t y=0;y<h;++y) for (uint32_t x=0;x<w;++x) for (unsigned ch=0;ch<4;++ch) {
        size_t i=(size_t(y)*2*sw+x*2)*4+ch;
        out[(size_t(y)*w+x)*4+ch]=uint8_t((unsigned(pixels[i])+pixels[i+4]+pixels[i+sw*4]+pixels[i+sw*4+4]+2)/4);
    }
    return JFX_SUCCESS;
}
