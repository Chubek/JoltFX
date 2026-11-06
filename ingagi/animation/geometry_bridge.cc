//go:build cgo

#include "geometry_bridge.h"
#include <2geom/bezier.h>
#include <algorithm>

extern "C" void ig_anim_bezier(const double *xy,unsigned degree,double t,double *out) {
    Geom::Point controls[4];
    for(unsigned i=0;i<=degree;++i) controls[i]=Geom::Point(xy[2*i],xy[2*i+1]);
    auto p=Geom::bernstein_value_at(t,controls,degree);
    out[0]=p.x(); out[1]=p.y();
}
extern "C" double ig_anim_segment_distance(double px,double py,double ax,double ay,double bx,double by) {
    Geom::Point p(px,py),a(ax,ay),b(bx,by),d=b-a;
    double n=d.lengthSq();
    double t=n>0?std::clamp(Geom::dot(p-a,d)/n,0.0,1.0):0;
    return Geom::distance(p,Geom::lerp(t,a,b));
}
