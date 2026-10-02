/* Execute the compiled Emscripten module through the production JS bridge.
 * The unit suite's FFI doubles cannot catch stack/heap/export/ABI failures. */
import test from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import { pathToFileURL } from "node:url";
import { EmscriptenJoltBridge } from "../dist/emscripten_bridge.js";

if (!process.env.JFX_WASM_MODULE) throw new Error("Set JFX_WASM_MODULE to the built joltfx_web.js module");
const createModule = (await import(pathToFileURL(resolve(process.env.JFX_WASM_MODULE)).href)).default;
const module = await createModule();
const fixture = name => readFileSync(new URL(`../../../tests/fixtures/${name}`, import.meta.url), "utf8");
const pixels = frame => [...frame.pixels];
function withBridge(run) {
  const bridge = new EmscriptenJoltBridge(module, 2, 1);
  try { return run(bridge); } finally { bridge.dispose(); }
}
function audioFixture() {
  const bytes=new Uint8Array(44+16000), v=new DataView(bytes.buffer), text=new TextEncoder();
  bytes.set(text.encode("RIFF")); v.setUint32(4,36+16000,true); bytes.set(text.encode("WAVEfmt "),8);
  v.setUint32(16,16,true); v.setUint16(20,1,true); v.setUint16(22,1,true); v.setUint32(24,8000,true);
  v.setUint32(28,16000,true); v.setUint16(32,2,true); v.setUint16(34,16,true);
  bytes.set(text.encode("data"),36); v.setUint32(40,16000,true);
  for (let i=0;i<8000;i++) v.setInt16(44+i*2,8192,true);
  return bytes;
}

test("real WASM exposes the typed node and kernel-backed color catalogs", () => withBridge(bridge => {
  const catalog = bridge.nodeKinds(), colors = bridge.colorOperators();
  assert.ok(colors.length >= 29);
  assert.ok(colors.find(color => color.name === "calib_lut").path);
  assert.ok(colors.find(color => color.name === "grade_primary").params.some(param => param.name === "exposure"));
  assert.equal(catalog.find(kind => kind.name === "solid").inputs[0].type, "color");
  assert.equal(catalog.find(kind => kind.name === "blend").outputs[0].type, "image");
  for (const color of colors) assert.ok(catalog.some(kind => kind.name === color.name));
  assert.equal(bridge.graphState().nodes[0].kind, "solid");
  assert.equal(bridge.sequenceState().tracks[0].clips[0].length, 300);
}));

test("real WASM graph edits, history and interior previews match native conformance", () => withBridge(bridge => {
  bridge.loadDocument(fixture("composition.jfx"));
  bridge.edit("node.param", 4, 0, 0, .5, "opacity");
  bridge.edit("node.add", 0, 0, 0, 0, "exposure");
  bridge.edit("node.connect", 4, 6, 0, 0);
  bridge.edit("node.param", 6, 0, 0, -1, "stops");
  bridge.edit("node.output", 6);
  bridge.edit("node.duplicate", 6);
  bridge.edit("node.param", 7, 0, 0, 0, "stops");
  bridge.edit("node.position", 6, 0, 0, -50, "200");
  bridge.edit("node.label", 6, 0, 0, 0, 'Final #Look 🎨 "quoted"');
  bridge.edit("node.remove", 5);
  bridge.edit("undo"); bridge.edit("redo");
  const before = bridge.graphState();
  assert.equal(before.output, 5);
  assert.equal(before.nodes[5].x, -50);
  assert.equal(before.nodes[5].label, 'Final #Look 🎨 "quoted"');
  assert.throws(() => bridge.edit("node.connect", 6, 4, 0, 0));
  assert.deepEqual(bridge.graphState(), before);
  assert.deepEqual(pixels(bridge.renderGraphNode(null, 0)), [32, 0, 96, 255, 32, 0, 96, 255]);
  assert.deepEqual(pixels(bridge.renderGraphNode(6, 0)), [64, 0, 191, 255, 64, 0, 191, 255]);
  assert.deepEqual(bridge.graphState(), before);
  const saved = bridge.saveDocument(); bridge.loadDocument(saved);
  assert.equal(bridge.graphState().nodes[5].label, before.nodes[5].label);
  assert.deepEqual(pixels(bridge.renderFrame(0)), [32, 0, 96, 255, 32, 0, 96, 255]);
}));

test("real WASM NLE and color edits preserve exact-frame rendering and independent graph state", () => withBridge(bridge => {
  bridge.loadDocument(fixture("nle_sequence.jfx"));
  bridge.edit("clip.split", 0, 0, 0, 6);
  bridge.edit("clip.move", 0, 1, 1, 4);
  bridge.edit("clip.slip", 1, 0, 0, 3);
  bridge.edit("clip.duplicate", 0, 1, 0, 24);
  bridge.edit("clip.ripple_delete", 0, 1); bridge.edit("undo");
  bridge.edit("calibration.add", 1, 0, 0, 0, "calib_lut");
  bridge.edit("grade.add", 1, 0, 0, 0, "grade_primary");
  bridge.edit("grade.param", 1, 0, 0, 1, "exposure");
  bridge.edit("undo"); bridge.edit("redo");
  assert.deepEqual(pixels(bridge.renderSequenceFrame(5)), [204, 102, 51, 255, 204, 102, 51, 255]);
  assert.deepEqual(pixels(bridge.renderFrame(5 / 30)), pixels(bridge.renderSequenceFrame(5)));
  const sequence = bridge.sequenceState(); bridge.edit("graph");
  assert.match(bridge.sequenceDocument(), /effect grade_primary/);
  bridge.edit("node.label", 0, 0, 0, 0, "Independent graph");
  assert.deepEqual(bridge.sequenceState().tracks, sequence.tracks);
  bridge.edit("undo"); assert.equal(bridge.graphState().active, true);
  bridge.edit("sequence");
  assert.deepEqual(pixels(bridge.renderSequenceFrame(5)), [204, 102, 51, 255, 204, 102, 51, 255]);
}));

test("real WASM virtual image/LUT resources propagate errors and survive heap growth", () => withBridge(bridge => {
  bridge.loadDocument('graph\nsize 2 1\nnode image "Image"\noutput 1\n');
  const ppm = new Uint8Array([...new TextEncoder().encode("P6\n2 1\n255\n"), 200, 100, 50, 20, 40, 80]);
  const image = bridge.importAsset("test image.ppm", ppm);
  bridge.edit("node.path", 0, 0, 0, 0, image);
  assert.deepEqual(pixels(bridge.renderGraphNode(null, 0)), [200, 100, 50, 255, 20, 40, 80, 255]);
  bridge.edit("node.add", 0, 0, 0, 0, "grade_lut"); bridge.edit("node.connect", 0, 1);
  const cube = "LUT_1D_SIZE 2\n0 0 0\n0.5 0.5 0.5\n";
  const lut = bridge.importAsset("half.cube", new TextEncoder().encode(cube));
  bridge.edit("node.path", 1, 0, 0, 0, lut); bridge.edit("node.output", 1);
  assert.deepEqual(pixels(bridge.renderGraphNode(null, 0)), [100, 50, 25, 255, 10, 20, 40, 255]);
  const retained = bridge.renderGraphNode(null, 0);
  const oldHeap = module.HEAPU8.buffer;
  const allocation = module._malloc(48 * 1024 * 1024); assert.ok(allocation);
  try {
    assert.notEqual(module.HEAPU8.buffer, oldHeap);
    assert.deepEqual(pixels(bridge.renderGraphNode(null, 0)), pixels(retained));
    assert.deepEqual(pixels(retained), [100, 50, 25, 255, 10, 20, 40, 255]);
  } finally { module._free(allocation); }
  bridge.edit("node.path", 0, 0, 0, 0, "/missing/image.ppm");
  assert.throws(() => bridge.renderGraphNode(null, 0), /Unable to render composition/);
  bridge.edit("undo");
  assert.deepEqual(pixels(bridge.renderGraphNode(null, 0)), pixels(retained));
  bridge.loadDocument("graph\nsize 2 1\noutput 0\n");
  assert.deepEqual(pixels(bridge.renderGraphNode(null, 0)), [0, 0, 0, 0, 0, 0, 0, 0]);
}));

test("real WASM sample-accurate audio persists controls and resets cached snapshots on edits", () => withBridge(bridge => {
  const path=bridge.importAsset("audio fixture.wav",audioFixture());
  bridge.edit("sequence.new",16,16,25,1); bridge.edit("track.add",0,0,0,0,"Audio");
  bridge.edit("clip.add",0,6,5,25,path);
  const first=bridge.renderAudio(1584,64,8000);
  assert.deepEqual([...first.slice(0,32)],Array(32).fill(0));
  assert.deepEqual([...first.slice(32)],Array(96).fill(.25));
  bridge.edit("clip.audio.gain",0,0,0,2); bridge.edit("clip.audio.pan",0,0,0,1);
  bridge.edit("clip.audio.fade_in",0,0,0,5);
  const mix=bridge.renderAudio(2400,64,8000);
  assert.equal(mix[0],0); assert.equal(mix[1],.25);
  bridge.edit("clip.split",0,0,0,7); assert.deepEqual(bridge.renderAudio(2400,64,8000),mix);
  const saved=bridge.saveDocument(); bridge.loadDocument(saved);
  assert.deepEqual(bridge.renderAudio(2400,64,8000),mix);
  bridge.edit("track.mute",0,0,0,1); assert.ok(bridge.renderAudio(2400,64,8000).every(sample => sample===0));
  bridge.edit("undo"); assert.deepEqual(bridge.renderAudio(2400,64,8000),mix);
  const replacement=audioFixture(); replacement.fill(0,44);
  assert.equal(bridge.importAsset("audio fixture.wav",replacement),path);
  assert.ok(bridge.renderAudio(2400,64,8000).every(sample=>sample===0));
  assert.throws(() => bridge.renderAudio(-1,64)); assert.throws(() => bridge.renderAudio(0,65537));
}));

test("real WASM encodes, cancels and decodes exported video plus timeline audio", () => withBridge(bridge => {
  if (!module.cwrap("jfx_export_available","number",[])()) {
    assert.throws(() => bridge.beginVideoExport("output.mp4",0,4),/Encoder unavailable/); return;
  }
  const path=bridge.importAsset("export audio.wav",audioFixture());
  bridge.edit("sequence.new",16,16,25,1); bridge.edit("track.add",0,0,0,0,"AV");
  bridge.edit("clip.add",0,0,0,4); bridge.edit("clip.add",0,6,0,4,path);
  const expected=pixels(bridge.renderSequenceFrame(0));
  const job=bridge.beginVideoExport("output.mkv",0,4,true);
  assert.equal(job.step().done,false);
  bridge.edit("clip.enabled",0,0,0,0); // snapshot remains the initial edit
  let result; do { result=job.step(); } while (!result.done);
  assert.equal(result.completed,4); assert.ok(result.bytes.byteLength>512);
  const encoded=bridge.importAsset("decoded.mkv",result.bytes); job.dispose();
  bridge.edit("sequence.new",16,16,25,1); bridge.edit("track.add",0,0,0,0,"Decode");
  bridge.edit("clip.add",0,5,0,4,encoded);
  assert.deepEqual(pixels(bridge.renderSequenceFrame(1)),expected);
  const pcm=bridge.renderAudio(100,64,8000); assert.ok(pcm.every(sample => Math.abs(sample-.25)<1e-4));
  const cancelled=bridge.beginVideoExport("cancelled.mp4",0,4,true);
  cancelled.step(); cancelled.cancel(); assert.throws(() => cancelled.step()); cancelled.dispose();
  const mp4=bridge.beginVideoExport("output.mp4",0,4,true);
  do { result=mp4.step(); } while (!result.done);
  assert.equal(new TextDecoder().decode(result.bytes.slice(4,8)),"ftyp"); mp4.dispose();
}));
