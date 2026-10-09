/* Private geometry generators. Storage uses the engine's allocator. A bicubic
 * rational patch is a clamped NURBS with knots [0,0,0,0,1,1,1,1] in U and V. */
glm::vec3 gvec(const Vec &v) { return {v[0],v[1],v[2]}; }
Vec vvec(const glm::vec3 &v) { return {v.x,v.y,v.z}; }
void grid_faces(Object &o,uint32_t rows,uint32_t columns,bool wrap_rows,bool wrap_columns) {
    for (uint32_t r=0;r<(wrap_rows?rows:rows-1);++r) for (uint32_t c=0;c<(wrap_columns?columns:columns-1);++c) {
        uint32_t a=r*columns+c,b=r*columns+(c+1)%columns;
        uint32_t d=((r+1)%rows)*columns+c,e=((r+1)%rows)*columns+(c+1)%columns;
        o.triangles.push_back({a,d,b}); o.triangles.push_back({b,d,e});
    }
}
// Orient convex solids away from an interior point, including the asymmetric
// wedge. Shared edge vertices keep subdivision and mesh interchange watertight.
void orient_convex(Object &o) {
    glm::vec3 center(0);
    for (auto &v:o.vertices) center+=gvec(v);
    center/=float(o.vertices.size());
    for (auto &f:o.triangles) {
        auto a=gvec(o.vertices[f[0]]),b=gvec(o.vertices[f[1]]),c=gvec(o.vertices[f[2]]);
        if (glm::dot(glm::cross(b-a,c-a),(a+b+c)/3.f-center)<0) std::swap(f[1],f[2]);
    }
}
void polyhedron(Object &o,const char *kind) {
    if (!std::strcmp(kind,"wedge")) {
        o.vertices={{{-1,-1,-1}},{{1,-1,-1}},{{-1,1,-1}},{{-1,-1,1}},{{1,-1,1}},{{-1,1,1}}};
        o.triangles={{{0,2,1}},{{3,4,5}},{{0,1,4}},{{0,4,3}},{{0,3,5}},{{0,5,2}},{{1,2,5}},{{1,5,4}}};
    } else if (!std::strcmp(kind,"tetrahedron")) {
        o.vertices={{{1,1,1}},{{-1,-1,1}},{{-1,1,-1}},{{1,-1,-1}}};
        o.triangles={{{0,1,2}},{{0,3,1}},{{0,2,3}},{{1,3,2}}};
        for (auto &v:o.vertices) v=vvec(glm::normalize(gvec(v)));
    } else if (!std::strcmp(kind,"octahedron")) {
        o.vertices={{{1,0,0}},{{-1,0,0}},{{0,1,0}},{{0,-1,0}},{{0,0,1}},{{0,0,-1}}};
        o.triangles={{{2,0,4}},{{2,4,1}},{{2,1,5}},{{2,5,0}},{{3,4,0}},{{3,1,4}},{{3,5,1}},{{3,0,5}}};
    } else {
        require(!std::strcmp(kind,"icosahedron"));
        const float g=float((1+std::sqrt(5.))/2);
        o.vertices={{{-1,g,0}},{{1,g,0}},{{-1,-g,0}},{{1,-g,0}},{{0,-1,g}},{{0,1,g}},
            {{0,-1,-g}},{{0,1,-g}},{{g,0,-1}},{{g,0,1}},{{-g,0,-1}},{{-g,0,1}}};
        o.triangles={{{0,11,5}},{{0,5,1}},{{0,1,7}},{{0,7,10}},{{0,10,11}},
            {{1,5,9}},{{5,11,4}},{{11,10,2}},{{10,7,6}},{{7,1,8}},
            {{3,9,4}},{{3,4,2}},{{3,2,6}},{{3,6,8}},{{3,8,9}},
            {{4,9,5}},{{2,4,11}},{{6,2,10}},{{8,6,7}},{{9,8,1}}};
        for (auto &v:o.vertices) v=vvec(glm::normalize(gvec(v)));
    }
    orient_convex(o);
}
void extra_primitive(Object &o,const char *kind,uint32_t n) {
    if (!std::strcmp(kind,"pyramid")) {
        o.vertices={{{-1,-1,-1}},{{1,-1,-1}},{{1,-1,1}},{{-1,-1,1}},{{0,1,0}}};
        o.triangles={{{0,1,2}},{{0,2,3}},{{0,4,1}},{{1,4,2}},{{2,4,3}},{{3,4,0}}};
    } else if (!std::strcmp(kind,"wedge") || !std::strcmp(kind,"tetrahedron") ||
        !std::strcmp(kind,"octahedron") || !std::strcmp(kind,"icosahedron")) {
        polyhedron(o,kind);
    } else if (!std::strcmp(kind,"torus")) {
        o.smooth=true; uint32_t rings=n/2;
        for (uint32_t r=0;r<n;++r) for (uint32_t c=0;c<rings;++c) {
            double u=2*pi*r/n,v=2*pi*c/rings;
            o.vertices.push_back({float((1+.35*std::cos(v))*std::cos(u)),float(.35*std::sin(v)),float((1+.35*std::cos(v))*std::sin(u))});
        }
        grid_faces(o,n,rings,true,true);
    } else if (!std::strcmp(kind,"tube")) {
        o.smooth=true;
        const float profile[4][2]={{1,1},{1,-1},{.6f,-1},{.6f,1}};
        for (auto &p:profile) for (uint32_t s=0;s<n;++s)
            o.vertices.push_back({p[0]*float(std::cos(2*pi*s/n)),p[1],p[0]*float(std::sin(2*pi*s/n))});
        // Outer wall, bottom annulus, inner wall and top annulus. Weld both
        // seams and preserve the opening instead of capping it with a disk.
        for (uint32_t r=0;r<4;++r) for (uint32_t s=0;s<n;++s) {
            uint32_t a=r*n+s,b=r*n+(s+1)%n,c=((r+1)%4)*n+s,d=((r+1)%4)*n+(s+1)%n;
            o.triangles.push_back({a,b,c}); o.triangles.push_back({b,d,c});
        }
    } else if (!std::strcmp(kind,"hemisphere")) {
        o.smooth=true; const uint32_t rings=n/4;
        o.vertices.push_back({0,1,0});
        for (uint32_t r=1;r<=rings;++r) for (uint32_t s=0;s<n;++s) {
            double theta=pi*.5*r/rings,phi=2*pi*s/n;
            o.vertices.push_back({float(std::sin(theta)*std::cos(phi)),r==rings?0.f:float(std::cos(theta)),float(std::sin(theta)*std::sin(phi))});
        }
        uint32_t bottom=uint32_t(o.vertices.size()),last=1+(rings-1)*n;
        o.vertices.push_back({0,0,0});
        for (uint32_t s=0;s<n;++s) {
            uint32_t next=(s+1)%n;
            o.triangles.push_back({0,1+next,1+s}); o.triangles.push_back({bottom,last+s,last+next});
            for (uint32_t r=0;r+1<rings;++r) {
                uint32_t a=1+r*n+s,b=1+r*n+next,c=a+n,d=b+n;
                o.triangles.push_back({a,b,c}); o.triangles.push_back({b,d,c});
            }
        }
    } else if (!std::strcmp(kind,"disk")) {
        o.vertices.push_back({0,0,0});
        for (uint32_t s=0;s<n;++s) o.vertices.push_back({float(std::cos(2*pi*s/n)),0,float(std::sin(2*pi*s/n))});
        for (uint32_t s=0;s<n;++s) o.triangles.push_back({0,1+(s+1)%n,1+s});
    } else {
        bool cone=!std::strcmp(kind,"cone"),cylinder=!std::strcmp(kind,"cylinder"),capsule=!std::strcmp(kind,"capsule");
        require(cone || cylinder || capsule); o.smooth=true;
        // Lathed profiles avoid duplicated seam vertices and collapsed pole quads.
        tilly::vector<std::array<float,2>> profile;
        float top=1,bottom=-1;
        if (capsule) {
            top=2; bottom=-2;
            uint32_t half=n/4;
            for (uint32_t r=1;r<=half;++r) {
                double angle=pi*.5*r/half; profile.push_back({float(std::sin(angle)),float(1+std::cos(angle))});
            }
            for (uint32_t r=0;r<half;++r) {
                double angle=pi*.5*r/half; profile.push_back({float(std::cos(angle)),float(-1-std::sin(angle))});
            }
        } else {
            if (cylinder) profile.push_back({1,1});
            profile.push_back({1,-1});
        }
        o.vertices.push_back({0,top,0});
        for (auto &p:profile) for (uint32_t s=0;s<n;++s)
            o.vertices.push_back({p[0]*float(std::cos(2*pi*s/n)),p[1],p[0]*float(std::sin(2*pi*s/n))});
        uint32_t pole=uint32_t(o.vertices.size()); o.vertices.push_back({0,bottom,0});
        for (uint32_t s=0;s<n;++s) {
            uint32_t next=(s+1)%n; o.triangles.push_back({0,1+next,1+s});
            uint32_t last=1+uint32_t(profile.size()-1)*n;
            o.triangles.push_back({pole,last+s,last+next});
            for (uint32_t r=0;r+1<profile.size();++r) {
                uint32_t a=1+r*n+s,b=1+r*n+next,c=a+n,d=b+n;
                o.triangles.push_back({a,b,c}); o.triangles.push_back({b,d,c});
            }
        }
    }
}
float ball_field(const Object &o,const glm::vec3 &p) {
    float field=0;
    for (auto &b:o.balls) {
        glm::vec3 delta=p-glm::vec3(b[0],b[1],b[2]);
        float t=std::max(0.f,1-glm::dot(delta,delta)/(2.25f*b[3]*b[3])); field+=t*t*t;
    }
    return field;
}
glm::vec3 ball_normal(const Object &o,const glm::vec3 &p) {
    glm::vec3 normal(0);
    for (auto &b:o.balls) {
        glm::vec3 delta=p-glm::vec3(b[0],b[1],b[2]); float r2=2.25f*b[3]*b[3];
        float t=std::max(0.f,1-glm::dot(delta,delta)/r2); normal+=delta*(6*t*t/r2);
    }
    return normal;
}
void regenerate(Object &o) {
    o.normals.reset();
    require(o.resolution>=8 && o.resolution<=64); o.vertices.clear(); o.triangles.clear();
    uint32_t n=o.resolution;
    if (o.generator==1) {
        require(o.controls.size()==16);
        for (uint32_t v=0;v<=n;++v) for (uint32_t u=0;u<=n;++u) {
            double x=double(u)/n,y=double(v)/n;
            double bu[4]={(1-x)*(1-x)*(1-x),3*x*(1-x)*(1-x),3*x*x*(1-x),x*x*x};
            double bv[4]={(1-y)*(1-y)*(1-y),3*y*(1-y)*(1-y),3*y*y*(1-y),y*y*y};
            glm::dvec3 point(0); double weight=0;
            for (unsigned j=0;j<4;++j) for (unsigned i=0;i<4;++i) {
                auto &p=o.controls[j*4+i]; require(p[3]>=.01f && p[3]<=100);
                double w=bu[i]*bv[j]*double(p[3]); point+=w*glm::dvec3(p[0],p[1],p[2]); weight+=w;
            }
            o.vertices.push_back(vvec(glm::vec3(point/weight)));
        }
        grid_faces(o,n+1,n+1,false,false);
    } else {
        require(o.generator==2 && !o.balls.empty() && o.balls.size()<=16);
        glm::vec3 lo(100000),hi(-100000);
        for (auto &b:o.balls) {
            require(b[3]>=.01f && b[3]<=100);
            glm::vec3 p(b[0],b[1],b[2]),radius(1.5f*b[3]); lo=glm::min(lo,p-radius); hi=glm::max(hi,p+radius);
        }
        // A common cubic lattice and six consistently split tetrahedra per cell.
        // Cache lattice-edge intersections so adjacent cells share their vertices.
        float extent=std::max({hi.x-lo.x,hi.y-lo.y,hi.z-lo.z});
        glm::vec3 center=(lo+hi)*.5f; lo=center-glm::vec3(extent*.5f); glm::vec3 step(extent/float(n));
        uint32_t side=n+1; size_t total=size_t(side)*side*side;
        tilly::vector<float> values(total);
        auto id=[&](uint32_t x,uint32_t y,uint32_t z) { return (z*side+y)*side+x; };
        auto position=[&](uint32_t i) { return lo+step*glm::vec3(float(i%side),float((i/side)%side),float(i/(side*side))); };
        for (uint32_t i=0;i<total;++i) values[i]=ball_field(o,position(i));
        struct EdgeSlot { uint64_t edge=UINT64_MAX; uint32_t vertex=0; };
        // At most MAX_VERTICES distinct crossings, leaving the table at <=50%
        // occupancy. Fixed-capacity probing keeps both storage and work bounded.
        tilly::vector<EdgeSlot> edges(JFX_3D_MAX_VERTICES*2u);
        auto crossing=[&](uint32_t a,uint32_t b) {
            float t=(.2f-values[a])/(values[b]-values[a]);
            uint32_t first=std::min(a,b),second=std::max(a,b);
            if (t<1e-6f) first=second=a; else if (t>1-1e-6f) first=second=b;
            uint64_t edge=(uint64_t(first)<<32)|second,hash=edge;
            hash^=hash>>33; hash*=UINT64_C(0xff51afd7ed558ccd); hash^=hash>>33;
            size_t slot=size_t(hash)&(edges.size()-1);
            while (edges[slot].edge!=UINT64_MAX && edges[slot].edge!=edge) slot=(slot+1)&(edges.size()-1);
            if (edges[slot].edge==edge) return edges[slot].vertex;
            require(o.vertices.size()<JFX_3D_MAX_VERTICES);
            uint32_t index=uint32_t(o.vertices.size()); o.vertices.push_back(vvec(glm::mix(position(a),position(b),t))); edges[slot]={edge,index}; return index;
        };
        auto face=[&](uint32_t a,uint32_t b,uint32_t c) {
            glm::vec3 p=gvec(o.vertices[a]),q=gvec(o.vertices[b]),r=gvec(o.vertices[c]),normal=glm::cross(q-p,r-p);
            if (glm::dot(normal,normal)<1e-16f) return;
            if (glm::dot(normal,ball_normal(o,(p+q+r)/3.f))<0) std::swap(b,c);
            require(o.triangles.size()<JFX_3D_MAX_TRIANGLES); o.triangles.push_back({a,b,c});
        };
        constexpr unsigned tetra[6][4]={{0,1,3,7},{0,3,2,7},{0,2,6,7},{0,6,4,7},{0,4,5,7},{0,5,1,7}};
        for (uint32_t z=0;z<n;++z) for (uint32_t y=0;y<n;++y) for (uint32_t x=0;x<n;++x) {
            uint32_t cube[8]={id(x,y,z),id(x+1,y,z),id(x,y+1,z),id(x+1,y+1,z),id(x,y,z+1),id(x+1,y,z+1),id(x,y+1,z+1),id(x+1,y+1,z+1)};
            for (auto &t:tetra) {
                uint32_t inside[4],outside[4],ni=0,no=0;
                for (auto v:t) { uint32_t i=cube[v]; if (values[i]>=.2f) inside[ni++]=i; else outside[no++]=i; }
                if (!ni || !no) continue;
                if (ni==1 || no==1) {
                    uint32_t a=ni==1?inside[0]:outside[0]; auto *rest=ni==1?outside:inside;
                    face(crossing(a,rest[0]),crossing(a,rest[1]),crossing(a,rest[2]));
                } else {
                    uint32_t a=crossing(inside[0],outside[0]),b=crossing(inside[0],outside[1]);
                    uint32_t c=crossing(inside[1],outside[0]),d=crossing(inside[1],outside[1]); face(a,b,c); face(b,d,c);
                }
            }
        }
    }
}
void compile_script(Script &script,const char *source) {
    require(source && std::strlen(source)<=4096);
    Script next; next.source=source;
    if (*source) {
        jolt_program_t *raw=nullptr; jolt_diagnostic_t diagnostic={}; diagnostic.size=sizeof(diagnostic);
        auto status=jolt_compile(source,&raw,&diagnostic);
        if (status==JOLT_ERR_MEMORY) throw std::bad_alloc();
        require(status==JOLT_OK);
        struct Owner { jolt_program_t *p; ~Owner() { jolt_program_destroy(p); } } owner{raw};
        size_t n=0; const uint8_t *bytes=jolt_program_data(raw,&n); require(bytes && n>=16);
        auto integer=[&](size_t offset) { uint32_t result=0; for (unsigned i=0;i<4;++i) result|=uint32_t(bytes[offset+i])<<(8*i); return result; };
        require(integer(8)<=4 && integer(12)==1); next.code.assign(bytes,bytes+n);
    }
    script=std::move(next);
}
glm::quat legacy_camera(const std::array<float,7> &camera) {
    return glm::angleAxis(glm::radians(camera[0]),glm::vec3(0,1,0))*glm::angleAxis(glm::radians(-camera[1]),glm::vec3(1,0,0));
}
glm::vec3 read_xyz(const char *text) {
    require(text); Input in(text); in.imbue(std::locale::classic()); glm::vec3 v; tilly::string extra;
    require(bool(in>>v.x>>v.y>>v.z) && !(in>>extra)); for (int i=0;i<3;++i) require(number(double(v[i]))); return v;
}
