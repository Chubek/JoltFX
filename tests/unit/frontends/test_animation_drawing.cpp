#include "../../../frontends/desktop/src/animation_drawing.h"
#include <cassert>
#include <cstdio>
#include <string>
using namespace jfx_drawing;
int main() {
    Editor e;
    Shape s; s.kind=Kind::Rectangle; s.points={{10,20},{100,80}};
    e.checkpoint(); e.cel().shapes.push_back(s);
    assert(e.hit({50,50})==0);
    assert(e.hit({200,50})==-1);
    e.frame=12; assert(e.cel().shapes.size()==1); // held exposure
    e.key(false); e.cel().shapes[0].points[0].x=30;
    e.frame=1; assert(e.cel().shapes[0].points[0].x==10);
    e.frame=24; e.key(true); assert(e.cel().shapes.empty());
    e.undo(); assert(e.cel().shapes.size()==1);
    e.redo(); assert(e.cel().shapes.empty());
    e.frame=12;
    assert(e.save("drawing-test.jfxdraw"));
    Editor loaded; assert(loaded.load("drawing-test.jfxdraw"));
    loaded.frame=12; assert(loaded.cel().shapes[0].points[0].x==30);
    loaded.frame=24; assert(loaded.cel().shapes.empty());
    loaded.frame=12; assert(loaded.export_svg("drawing-test.svg"));
    FILE *svg=std::fopen("drawing-test.svg","rb"); assert(svg); char output[2048]{};
    assert(std::fread(output,1,sizeof(output)-1,svg)>0); std::fclose(svg);
    assert(std::string(output).find("<rect x=\"30\"")!=std::string::npos);
    assert(loaded.save("drawing-test.jfxdraw")); // safely replace an existing drawing
    assert(!loaded.save("missing-drawing-directory/file.jfxdraw"));
    FILE *bad=std::fopen("drawing-test.bad","w"); assert(bad);
    std::fputs("JFXDRAW1 999999999 24 1",bad); std::fclose(bad);
    assert(!loaded.load("drawing-test.bad"));
    loaded.frame=12; assert(loaded.cel().shapes.size()==1);
    loaded.undo(); assert(loaded.cel().shapes.empty()); // load is one undo step
    // Unfilled shapes select only their outline; ellipses reject bounding-box corners.
    Editor outlines; s.filled=false; outlines.cel().shapes.push_back(s);
    assert(outlines.hit({50,50})==-1); assert(outlines.hit({10,50})==0);
    outlines.cel().shapes[0].kind=Kind::Ellipse;
    assert(outlines.hit({10,20})==-1); assert(outlines.hit({10,50})==0);
    outlines.doc.layers[0].locked=true; outlines.frame=3; outlines.key(true);
    assert(outlines.doc.layers[0].cels.size()==1);
    FILE *nan=std::fopen("drawing-test.bad","w"); assert(nan);
    std::fputs("JFXDRAW1 120 24 1\n1 0 1\n1 1\n2 0 0 nan 1 2\n0 0\n1 1\n",nan); std::fclose(nan);
    assert(!loaded.load("drawing-test.bad"));
    std::remove("drawing-test.jfxdraw"); std::remove("drawing-test.svg"); std::remove("drawing-test.bad");
}
