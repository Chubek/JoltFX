/* Private scene geometry tools. All edits operate on a candidate document;
 * validation and history publication remain in jfx_scene3d_command. */
tilly::vector<unsigned int> mesh_indices(const Object &o) {
    tilly::vector<unsigned int> indices; indices.reserve(o.triangles.size()*3);
    for (auto &t:o.triangles) for (auto i:t) indices.push_back(i);
    return indices;
}
void optimize_mesh(Object &o) {
    auto indices=mesh_indices(o);
    meshopt_optimizeVertexCache(indices.data(),indices.data(),indices.size(),o.vertices.size());
    tilly::vector<Vec> vertices(o.vertices.size());
    auto count=meshopt_optimizeVertexFetch(vertices.data(),indices.data(),indices.size(),
        o.vertices.data(),o.vertices.size(),sizeof(Vec));
    vertices.resize(count); o.vertices=std::move(vertices);
    for (size_t i=0;i<indices.size();++i) o.triangles[i/3][i%3]=indices[i];
    o.normals.reset();
}
void simplify_mesh(Object &o,float ratio) {
    auto indices=mesh_indices(o); tilly::vector<unsigned int> result(indices.size());
    size_t target=std::max(size_t(4),size_t(double(o.triangles.size())*double(ratio)))*3;
    if (target>=indices.size()) return;
    // Preserve borders/topology; error is relative to the source bounding box.
    // A target is best effort: never delete arbitrary triangles to force it.
    auto count=meshopt_simplify(result.data(),indices.data(),indices.size(),o.vertices[0].data(),
        o.vertices.size(),sizeof(Vec),target,.1f,meshopt_SimplifyLockBorder,nullptr);
    require(count>=3 && count%3==0); o.triangles.resize(count/3);
    for (size_t i=0;i<count;++i) o.triangles[i/3][i%3]=result[i];
    o.normals.reset();
}
void convex_hull(Object &o) {
    btConvexHullComputer hull;
    require(hull.compute(o.vertices[0].data(),int(sizeof(Vec)),int(o.vertices.size()),0,0)>=0);
    require(hull.vertices.size()>=4 && hull.faces.size()>=4);
    o.vertices.clear(); o.triangles.clear();
    for (int i=0;i<hull.vertices.size();++i) {
        auto &v=hull.vertices[i]; o.vertices.push_back({float(v.x()),float(v.y()),float(v.z())});
    }
    for (int i=0;i<hull.faces.size();++i) {
        const auto *start=&hull.edges[hull.faces[i]],*edge=start->getNextEdgeOfFace();
        uint32_t a=uint32_t(start->getSourceVertex()),b=uint32_t(edge->getSourceVertex());
        for (edge=edge->getNextEdgeOfFace();edge!=start;edge=edge->getNextEdgeOfFace()) {
            uint32_t c=uint32_t(edge->getSourceVertex()); o.triangles.push_back({a,b,c}); b=c;
        }
    }
    o.normals.reset();
}
void bake_path(Object &o,uint32_t frames,uint32_t first,uint32_t last,const char *text) {
    require(text && first<last && last<frames && (last-first+1)*3<=JFX_3D_MAX_KEYS);
    Input in(text); in.imbue(std::locale::classic()); glm::vec3 p[4];
    for (auto &point:p) for (int i=0;i<3;++i) require(bool(in>>point[i]) && number(double(point[i])));
    tilly::string extra; require(!(in>>extra));
    // Replace position keys within the requested range; retain all other keys.
    // Existing drivers would override the path, so explicitly reject them.
    for (unsigned i=0;i<3;++i) require(o.scripts[i].source.empty());
    o.keys.erase(std::remove_if(o.keys.begin(),o.keys.end(),[&](const Key &k) {
        return k.channel<3 && k.frame>=first && k.frame<=last;
    }),o.keys.end());
    for (uint32_t frame=first;frame<=last;++frame) {
        auto position=glm::catmullRom(p[0],p[1],p[2],p[3],float(frame-first)/float(last-first));
        for (unsigned ch=0;ch<3;++ch) put_key(o,ch,frame,position[int(ch)],1);
    }
}
glm::vec3 position3(const Vec &v) { return {v[0],v[1],v[2]}; }
// Ray direction need not be normalized: its parameter remains camera depth
// after inverse nonuniform object scaling. Double arithmetic protects grazing hits.
bool sculpt_hit(const Object &o,const glm::vec3 &origin,const glm::vec3 &direction,
                glm::vec3 &point,glm::vec3 &normal,double &out_depth) {
    double closest=std::numeric_limits<double>::max(); bool hit=false;
    for (auto &face:o.triangles) {
        glm::dvec3 a(position3(o.vertices[face[0]])),b(position3(o.vertices[face[1]])),c(position3(o.vertices[face[2]]));
        auto e1=b-a,e2=c-a,p=glm::cross(glm::dvec3(direction),e2);
        double det=glm::dot(e1,p); if (std::abs(det)<1.e-14) continue;
        auto delta=glm::dvec3(origin)-a; double u=glm::dot(delta,p)/det;
        if (u<0 || u>1) continue;
        auto q=glm::cross(delta,e1); double v=glm::dot(glm::dvec3(direction),q)/det;
        if (v<0 || u+v>1) continue;
        double depth=glm::dot(e2,q)/det;
        if (depth<.05 || depth>=closest) continue;
        closest=depth; point=origin+float(depth)*direction;
        normal=glm::vec3(glm::normalize(glm::cross(e1,e2))); hit=true;
    }
    out_depth=closest; return hit;
}
void sculpt(const Document &d,Object &o,uint32_t brush,uint32_t mirror,double strength,const char *text) {
    require(!o.generator && o.visible && o.instances==1 && brush<5 && mirror<=1 &&
        std::abs(strength)<=1 && text);
    // Normalized viewport X/Y (top-left origin), aspect, local radius, playhead.
    float x,y,aspect,radius; double seconds;
    Input in(text); in.imbue(std::locale::classic()); tilly::string extra;
    require(bool(in>>x>>y>>aspect>>radius>>seconds) && !(in>>extra));
    require(number(double(x)) && x>=0 && x<=1 && number(double(y)) && y>=0 && y<=1 &&
        number(double(aspect)) && aspect>=.01f && aspect<=100 && number(double(radius)) && radius>=.001f && radius<=1000 &&
        number(seconds) && seconds>=0);
    auto t=sample_instance(o,0,seconds,d.fps); auto inverse=glm::conjugate(rotation(t));
    glm::vec3 scale(t[6],t[7],t[8]),translation(t[0],t[1],t[2]);
    auto &camera=d.camera;
    glm::vec3 eye=glm::vec3(camera[3],camera[4],camera[5])+d.orientation*glm::vec3(0,0,camera[2]);
    float tangent=std::tan(glm::radians(camera[6])*.5f);
    glm::vec3 ray=d.orientation*glm::vec3((2*x-1)*aspect*tangent,(1-2*y)*tangent,-1);
    glm::vec3 center,normal; double depth;
    if (!sculpt_hit(o,(inverse*(eye-translation))/scale,(inverse*ray)/scale,center,normal,depth) || strength==0) return;
    // Respect the visible scene, including animated cloners in front of the
    // selected surface. Inverse transforms preserve camera-depth ray parameters.
    for (const auto &other:d.objects) if (&other!=&o && other.visible) {
        for (uint32_t instance=0;instance<other.instances;++instance) {
            auto pose=sample_instance(other,instance,seconds,d.fps);
            auto inv=glm::conjugate(rotation(pose));
            glm::vec3 size(pose[6],pose[7],pose[8]),offset(pose[0],pose[1],pose[2]),hit,axis;
            double other_depth;
            if (sculpt_hit(other,(inv*(eye-offset))/size,(inv*ray)/size,hit,axis,other_depth) && other_depth<depth-1.e-5) return;
        }
    }
    Eigen::MatrixXd vertices,normals; Eigen::MatrixXi faces; matrices(o,vertices,faces);
    igl::per_vertex_normals(vertices,faces,igl::PER_VERTEX_NORMALS_WEIGHTING_TYPE_AREA,normals);
    tilly::vector<glm::vec3> sums; tilly::vector<unsigned> counts;
    if (brush==2) {
        sums.resize(o.vertices.size(),glm::vec3(0)); counts.resize(o.vertices.size(),0);
        for (auto &face:o.triangles) for (unsigned j=0;j<3;++j) {
            auto a=face[j],b=face[(j+1)%3],c=face[(j+2)%3];
            sums[a]+=position3(o.vertices[b])+position3(o.vertices[c]); counts[a]+=2;
        }
    }
    auto result=o.vertices;
    for (size_t i=0;i<o.vertices.size();++i) {
        glm::vec3 p=position3(o.vertices[i]),n(float(normals(Eigen::Index(i),0)),float(normals(Eigen::Index(i),1)),float(normals(Eigen::Index(i),2)));
        float best=0; glm::vec3 displacement(0);
        for (unsigned side=0;side<(mirror?2u:1u);++side) {
            auto at=center,axis=normal; if (side) { at.x=-at.x; axis.x=-axis.x; }
            float distance=glm::length(p-at); if (distance>=radius || glm::dot(n,axis)<=0) continue;
            float w=1-distance/radius; w=w*w*(3-2*w);
            if (w<=best) continue;
            best=w;
            float amount=float(strength)*w;
            if (brush==0) displacement=axis*(radius*.15f*amount);
            else if (brush==1) displacement=n*(radius*.15f*amount);
            else if (brush==2) displacement=counts[i]?(sums[i]/float(counts[i])-p)*std::abs(amount)*.5f:glm::vec3(0);
            else if (brush==3) displacement=-axis*glm::dot(p-at,axis)*std::abs(amount)*.5f;
            else displacement=(at-p-axis*glm::dot(at-p,axis))*amount*.25f;
        }
        auto moved=p+displacement;
        if (mirror && std::abs(p.x)<1.e-6f) moved.x=p.x; // Keep the symmetry seam welded.
        result[i]={moved.x,moved.y,moved.z};
    }
    o.vertices=std::move(result); o.normals.reset();
}
