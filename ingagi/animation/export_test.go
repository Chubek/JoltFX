package animation

import (
	"math"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
)

func TestHeldCelsLayersAndMalformedDrawing(t *testing.T) {
	text := "JFXDRAW1 3 24 2\n1 0 2\n1 1\n2 0 4278190335 1 1 2\n0 0\n10 10\n3 0\n0 1 1\n1 1\n1 0 0 1 0 2\n0 0\n20 20\n"
	d, err := ReadDrawing(text)
	if err != nil {
		t.Fatal(err)
	}
	if len(d.Frames[0]) != 1 || len(d.Frames[1]) != 1 || len(d.Frames[2]) != 0 {
		t.Fatal(d)
	}
	for _, bad := range []string{text + "junk", strings.Replace(text, "3 0", "1 0", 1), strings.Replace(text, "0 0\n10 10", "NaN 0\n10 10", 1), strings.Replace(text, "1 1 2", "1 1 999999999999999999", 1)} {
		if _, err := ReadDrawing(bad); err == nil {
			t.Fatal("accepted malformed drawing")
		}
	}
	d.Frames[0][0].Points[0].X = math.Inf(1)
	if _, err = d.HTML(); err == nil {
		t.Fatal("nonfinite HTML")
	}
}

// Exercise the generated player, not a separately maintained renderer. A
// command recorder verifies ordering, alpha, curves, seeking and RAF ownership.
func TestCanvasPlayer(t *testing.T) {
	node, err := exec.LookPath("node")
	if err != nil {
		t.Skip("Node unavailable")
	}
	d := Document{FPS: 24, Frames: [][]Shape{
		{{Kind: 2, Layer: 1, Points: []Point{{10, 20}, {30, 40}}, Fill: 0x800000ff, Width: 1}, {Kind: 3, Layer: 0, Points: []Point{{0, 0}, {8, 8}}, Stroke: 0xff00ff00, Width: 2}},
		{{Kind: 4, Points: []Point{{0, 0}, {5, 0}, {2, 5}}, Fill: 0xffffffff, Width: 1}}, nil,
	}}
	html, err := d.HTML()
	if err != nil {
		t.Fatal(err)
	}
	code := strings.Split(strings.Split(html, "<script>\n")[1], "\n</script>")[0]
	harness := `const assert=require('node:assert/strict');let ops=[],raf=new Map(),next=0;
const mockCtx=new Proxy({}, {get:(o,k)=>o[k]||((...a)=>ops.push([k,...a])),set:(o,k,v)=>(o[k]=v,true)});
const nodes={animation:{width:960,height:540,getContext:()=>mockCtx},seek:{},play:{},frame:{}};
global.document={getElementById:k=>nodes[k]};global.performance={now:()=>1000};
global.requestAnimationFrame=f=>{raf.set(++next,f);return next;};global.cancelAnimationFrame=i=>raf.delete(i);
`
	checks := `
assert.equal(raf.size,1);ingagiAnimation.play();assert.equal(raf.size,1);
assert.ok(ops.findIndex(x=>x[0]==='ellipse')<ops.findIndex(x=>x[0]==='rect'));
assert.equal(ctx.fillStyle,'rgba(255,0,0,0.5019607843137255)');
ingagiAnimation.pause();assert.equal(raf.size,0);ops=[];
ingagiAnimation.seek(1);assert.equal(ingagiAnimation.frame,1);assert.ok(ops.some(x=>x[0]==='closePath'));
ops=[];ingagiAnimation.seek(2);assert.deepEqual(ops,[['clearRect',0,0,960,540]]);
ingagiAnimation.seek(NaN);assert.equal(ingagiAnimation.frame,2);
ingagiAnimation.seek(-2);assert.equal(ingagiAnimation.frame,0);
ingagiAnimation.play();const callback=[...raf.values()][0];raf.clear();callback(1125);
assert.equal(ingagiAnimation.frame,0);assert.equal(raf.size,1);ingagiAnimation.pause();
`
	path := filepath.Join(t.TempDir(), "canvas.cjs")
	if err = os.WriteFile(path, []byte(harness+code+checks), 0600); err != nil {
		t.Fatal(err)
	}
	if out, err := exec.Command(node, path).CombinedOutput(); err != nil {
		t.Fatalf("%v\n%s", err, out)
	}
}
