package runtime

import "testing"

func TestAnimationBuiltins(t *testing.T) {
	v, _ := runSrc(t, `fn frame(t: f64) {
 let p=anim_curve([vec2(0,0),vec2(0,10),vec2(10,10),vec2(10,0)],16);
 let q=anim_translate(p,vec2(20,30));
 assert(p[0].x==0);
 assert(q[0].x==20);
 assert(anim_track([0.0,1.0],[0.0,100.0],t,"smooth")==50.0);
 let color=anim_rgba(1,0,0,0.5);
 return [anim_path(q,false,0,color,3),anim_rect(vec2(0,0),vec2(10,20),color,0,1)];
 }
 fn main(){return frame(0.5);}`, "main")
	shapes, err := AnimationFrame(v)
	if err != nil || len(shapes) != 2 || shapes[1].Fill != 0x800000ff {
		t.Fatal(shapes, err)
	}
	vm := New()
	for _, name := range []string{"anim_curve", "anim_rect", "anim_track", "anim_rgba", "anim_layer"} {
		if _, err := vm.builtins[name](vm, nil); err == nil {
			t.Fatal(name)
		}
	}
	for _, v := range []Value{{K: KArray}, {K: KArray, O: &ArrayObj{Elems: []Value{{K: KMap}}}}} {
		if _, err := AnimationFrame(v); err == nil {
			t.Fatal("malformed frame")
		}
	}
}

func TestAnimationTransformsLayersAndOwnership(t *testing.T) {
	v, _ := runSrc(t, `fn main() {
 let p=[vec2(0,0),vec2(10,0),vec2(10,10)];
 let q=anim_transform(p,[1.0,0.0,0.0,1.0,4.0,5.0]);
 assert(q[2].x==14 && q[2].y==15);
 let scaled=anim_scale(p,vec2(2,3),vec2(10,10));
 assert(scaled[0].x==-10 && scaled[0].y==-20);
 let rotated=anim_rotate(p,0.0,vec2(10,10));
 assert(rotated[2].x==10 && rotated[2].y==10);
 let morph=anim_morph(p,q,0.5); assert(morph[0].x==2);
 let arc=anim_arc(vec2(10,20),vec2(5,8),0.0,1.0,8);
 assert(len(arc)==9 && arc[0].x==15 && arc[0].y==20);
 assert(anim_length([vec2(0,0),vec2(3,4)])==5);
 assert(anim_loop(-0.5,2)==0.75 && anim_pingpong(3,2)==0.5);
 let shape=anim_path(p,true,anim_rgba(1,0,0,1),0,1);
 let faded=anim_opacity(shape,0.5);
 let layer=anim_layer([faded],3);
 layer[0].points[0]=vec2(99,99);
 assert(p[0].x==0 && shape.points[0].x==0 && faded.points[0].x==0);
 assert(shape.layer==0 && layer[0].layer==3);
 return layer;
}`, "main")
	shapes, err := AnimationFrame(v)
	if err != nil || shapes[0].Layer != 3 || shapes[0].Fill>>24 != 128 {
		t.Fatal(shapes, err)
	}
}
