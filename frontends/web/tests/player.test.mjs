import assert from "node:assert/strict";
import test from "node:test";
import { JoltPlayer } from "../dist/player.js";
import { EmscriptenJoltBridge } from "../dist/emscripten_bridge.js";
import { JoltEditor, colorLayers } from "../dist/editor.js";
import { NLETimeline, timelineHit, snapFrame } from "../dist/nle.js";
import { CompositionCanvas, graphHit } from "../dist/composition.js";

globalThis.ImageData = class ImageData {
  constructor(data, width, height) { this.data = data; this.width = width; this.height = height; }
};
globalThis.cancelAnimationFrame = () => {};
globalThis.requestAnimationFrame = () => 1;

function canvas() {
  return { width: 0, height: 0, getContext: () => ({ putImageData: () => {} }) };
}
const bridge = {
  loadPackage: () => {},
  renderFrame: () => ({ width: 1, height: 1, pixels: new Uint8ClampedArray([1, 2, 3, 255]) }),
};

test("web player loads, plays, seeks and ends", async () => {
  const player = new JoltPlayer(canvas(), bridge);
  await player.load(new Uint8Array([1]), 2);
  let ended = 0;
  player.on("end", () => { ended += 1; });
  player.play();
  player.advance(1);
  assert.equal(player.time, 1);
  player.advance(2);
  assert.equal(player.time, 2);
  assert.equal(ended, 1);
  assert.equal(player.playing, false);
  player.seek(0.5);
  assert.equal(player.time, 0.5);
});

test("Emscripten bridge passes a JSON effect envelope to Core", async () => {
  const heap = new Uint8Array(8192);
  let effect = "";
  const module = {
    HEAPU8: heap,
    _malloc: () => 128,
    _free: () => {},
    cwrap(name) {
      if (name === "jfx_web_session_create") return (_backend, outputPointer) => {
        new DataView(heap.buffer).setUint32(outputPointer, 9, true); return 0;
      };
      if (name === "jfx_web_session_destroy") return () => 0;
      if (name === "jfx_web_session_set_effect") return (_session, selected) => { effect = selected; return 0; };
      if (name === "jfx_web_session_render_rgba") return (_session, _time, width, height, pointer) => {
        heap.fill(255, pointer, pointer + width * height * 4); return 0;
      };
      throw new Error(`unexpected export ${name}`);
    },
  };
  const bridge = new EmscriptenJoltBridge(module, 1, 1);
  await bridge.loadPackage(new TextEncoder().encode('{"effect":"invert"}'));
  assert.equal(effect, "invert");
  assert.equal(bridge.renderFrame(0).pixels[0], 255);
  bridge.dispose();
});

test("color sections follow clip selection, quoted LUTs and bypass indices", () => {
  const catalog = [
    { name: "grade_primary", label: "Primary", section: "grade", params: [] },
    { name: "grade_lut", label: "LUT", section: "grade", path: true, params: [] },
    { name: "calib_lut", label: "Calibration LUT", section: "calibration", path: true, params: [] },
  ];
  const document = `track V1
clip solid 0 300
effect invert
effect grade_primary 1 1 .5 1
effect calib_lut "" 1 1
effect grade_lut "/look with # space.cube" .25 1
disable 1 4
clip solid 0 300
effect grade_primary -1 1 .5 1
track V2
clip solid 0 300
effect grade_lut "" 1 1
`;
  const grades = catalog.filter(op => op.section === "grade");
  const first = colorLayers(document, 0, 0, grades);
  assert.deepEqual(first.map(layer => layer.op.name), ["grade_primary", "grade_lut"]);
  assert.equal(first[1].words[2], "/look with # space.cube");
  assert.equal(first[1].words[3], ".25");
  assert.equal(first[1].enabled, false);
  assert.equal(first[0].enabled, true);
  assert.equal(colorLayers(document, 0, 1, grades)[0].words[2], "-1");
  assert.equal(colorLayers(document, 1, 0, grades)[0].words[2], "");
  assert.equal(colorLayers(document, 0, 0, catalog.filter(op => op.section === "calibration")).length, 1);
  assert.equal(colorLayers(document, 9, 9, grades).length, 0);
});

test("NLE hit-testing follows visible clip order and snap tolerance", () => {
  const state = { tracks: [{ clips: [{ start: 0, length: 30 }, { start: 10, length: 10 }] }] };
  assert.equal(timelineHit(state, 0, 9), 0);
  assert.equal(timelineHit(state, 0, 10), 1);
  assert.equal(timelineHit(state, 0, 20), 0);
  assert.equal(timelineHit(state, 0, 30), -1);
  assert.equal(timelineHit(state, 5, 10), -1);
  assert.equal(snapFrame(29, [0, 30, 35], 3), 30);
  assert.equal(snapFrame(25, [30], 3), 25);
  assert.equal(snapFrame(-10, [], 3), 0);
});

test("NLE drag commits one native edit and edge trim preserves the end", () => {
  const clip = { name: "Shot", source: "solid", path: "", start: 0, length: 30, inPoint: 0, enabled: true, opacity: 1, effects: 0 };
  const state = { width: 2, height: 1, fpsNum: 30000, fpsDen: 1001, duration: 60, canUndo: false, canRedo: false,
    tracks: [{ name: "V1", muted: false, solo: false, opacity: 1, clips: [clip] }] };
  const calls = [], selections = [];
  const context = new Proxy({}, { get: () => () => {}, set: () => true });
  const canvas = { width: 900, height: 64, style: {}, setAttribute() {}, setPointerCapture() {},
    getContext: () => context, getBoundingClientRect: () => ({ left: 0, top: 0, width: 900, height: 64 }) };
  const timeline = new NLETimeline(canvas, (a, b) => selections.push([a, b]), () => {}, (...args) => calls.push(args));
  timeline.update(state, 0, 0, 0);
  canvas.onpointerdown({ clientX: 140, clientY: 40, pointerId: 1 });
  canvas.onpointermove({ clientX: 160, clientY: 40 });
  assert.equal(calls.length, 0);
  canvas.onpointerup({});
  assert.deepEqual(selections[0], [0, 0]);
  assert.deepEqual(calls[0], ["clip.move", 0, 0, 0, 10]);
  canvas.onpointerdown({ clientX: 122, clientY: 40, pointerId: 1 });
  canvas.onpointermove({ clientX: 142, clientY: 40 }); canvas.onpointerup({});
  assert.deepEqual(calls[1], ["clip.trim", 0, 0, 10, 20]);
  canvas.onpointerdown({ clientX: 140, clientY: 40, pointerId: 1 });
  canvas.onpointercancel({}); canvas.onpointerup({}); assert.equal(calls.length, 2);
  canvas.onpointerdown({ clientX: 178, clientY: 40, pointerId: 1 });
  canvas.onpointerup({}); assert.equal(calls.length, 2, "clicking a tail must not trim the clip");
});

test("NLE exact-frame bridge validates indices and releases failed render buffers", () => {
  const heap = new Uint8Array(8192), edits = [], frames = [], freed = [];
  let failed = false;
  const module = {
    HEAPU8: heap, _malloc: () => 128, _free: pointer => freed.push(pointer),
    cwrap(name) {
      if (name === "jfx_web_session_create") return (_, pointer) => { new DataView(heap.buffer).setUint32(pointer, 9, true); return 0; };
      if (name === "jfx_web_session_destroy" || name === "jfx_web_session_set_effect" || name === "jfx_web_session_render_rgba") return () => 0;
      if (name === "jfx_web_session_edit") return (...args) => { edits.push(args); return 0; };
      if (name === "jfx_web_session_render_frame") return (_, frame, width, height, pointer) => {
        frames.push(frame); heap.fill(frame & 255, pointer, pointer + width * height * 4); return failed ? 1 : 0;
      };
      throw new Error(`unexpected export ${name}`);
    },
  };
  const bridge = new EmscriptenJoltBridge(module, 1, 1);
  const pixels = bridge.renderSequenceFrame(17).pixels;
  assert.equal(pixels[0], 17); assert.deepEqual(frames, [17]);
  bridge.edit("sequence.new", 320, 180, 30000, 1001);
  assert.equal(edits.length, 1);
  assert.throws(() => bridge.edit("sequence.new", 320.5, 180, 30, 1), RangeError);
  assert.throws(() => bridge.edit("clip.move", 0, -1, 0, 1), RangeError);
  assert.throws(() => bridge.renderSequenceFrame(0x100000000), RangeError);
  assert.throws(() => bridge.renderSequenceFrame(0.5), RangeError);
  failed = true; const before = freed.length;
  assert.throws(() => bridge.renderSequenceFrame(20), /Unable to render/);
  assert.equal(freed.length, before + 1); assert.equal(pixels[0], 17);
  bridge.dispose();
});

class Element {
  constructor(tag) { this.tag = tag; this.children = []; this.options = this.children; this.style = {}; this.selectedIndex = -1; this.classList = { add() {} }; this.textContent = ""; this.handlers = {}; }
  append(...children) { this.children.push(...children); if (this.tag === "select" && this.selectedIndex < 0) this.selectedIndex = 0; }
  replaceChildren(...children) { this.children = children; this.options = this.children; this.selectedIndex = -1; }
  set value(value) { if (this.tag === "select") this.selectedIndex = this.options.findIndex(option => option.value === value); else this._value = value; }
  get value() { return this.tag === "select" ? this.options[this.selectedIndex]?.value ?? "" : this._value ?? (this.tag === "option" ? this.textContent : ""); }
  setAttribute() {} addEventListener(event, handler) { this.handlers[event] = handler; } removeEventListener(event) { delete this.handlers[event]; }
  getContext() { return new Proxy({}, { get: () => () => {}, set: () => true }); }
}
test("mounted editor can add the first color operator and uses the rendered frame for edits", () => {
  const previousDocument = globalThis.document;
  globalThis.document = { createElement: tag => new Element(tag), createTextNode: text => ({ textContent: text }) };
  try {
    const calls = [], root = new Element("main");
    const state = { width: 1, height: 1, fpsNum: 30, fpsDen: 1, duration: 60, canUndo: false, canRedo: false,
      tracks: [{ name: "V1", muted: false, solo: false, opacity: 1, clips: [{ name: "Shot", start: 0, length: 60 }] }] };
    const editor = new JoltEditor(root, {
      ...bridge, edit(...args) { assert.ok(args.slice(1, 4).every(i => i >= 0 && Number.isInteger(i))); calls.push(args); },
      saveDocument: () => "track V1\nclip solid 0 60\n", sequenceDocument: () => "track V1\nclip solid 0 60\n", sequenceState: () => state,
      colorOperators: () => [{ name: "grade_primary", label: "Primary", section: "grade", params: [] }],
      nodeKinds: () => [], graphState: () => ({ active: false, width: 1, height: 1, output: null, nodes: [] }),
    });
    const grading = root.children.find(section => section.children?.[0]?.textContent === "Color Grading");
    grading.children.find(element => element.textContent === "Add").onclick();
    assert.deepEqual(calls.at(-1), ["grade.add", 0, 0, 0, 0, "grade_primary"]);
    // Playback between frame boundaries must display and split the frame Core renders.
    editor.time = 10.7 / 30; editor.render();
    const nle = root.children.find(section => section.children?.[0]?.textContent === "NLE timeline");
    nle.children.find(element => element.textContent === "Split at playhead").onclick();
    assert.deepEqual(calls.at(-1), ["clip.split", 0, 0, 0, 10]);
    editor.dispose();
  } finally { globalThis.document = previousDocument; }
});

const nodeCatalog = [
  { name: "color", label: "Color", category: "Utility", inputs: [], outputs: [{ name: "out", label: "Color", type: "color" }], params: [], strings: [] },
  { name: "solid", label: "Solid", category: "Source", inputs: [{ name: "color", label: "Color", type: "color" }], outputs: [{ name: "out", label: "Image", type: "image" }], params: [], strings: [] },
];
const graphState = { active: true, width: 2, height: 1, output: 1, canUndo: false, canRedo: false, nodes: [
  { kind: "color", label: "Color #1", x: 0, y: 0, values: [], strings: [], inputs: [] },
  { kind: "solid", label: "Image", x: 240, y: 0, values: [], strings: [], inputs: [null] },
] };
test("composition hit-testing distinguishes typed ports and covered nodes", () => {
  assert.deepEqual(graphHit(graphState, nodeCatalog, 210, 44), { node: 0, side: "output", port: 0 });
  assert.deepEqual(graphHit(graphState, nodeCatalog, 240, 44), { node: 1, side: "input", port: 0 });
  assert.deepEqual(graphHit(graphState, nodeCatalog, 260, 20), { node: 1 });
  assert.equal(graphHit(graphState, nodeCatalog, -20, 0), undefined);
  const covered = { ...graphState, nodes: graphState.nodes.map(node => ({ ...node, x: 0 })) };
  assert.deepEqual(graphHit(covered, nodeCatalog, 30, 20), { node: 1 });
});
test("composition canvas commits one layout edit and typed edge per drag", () => {
  const context = new Proxy({}, { get: () => () => {}, set: () => true });
  const calls = [], selections = [];
  const canvas = { width: 900, height: 360, style: {}, setAttribute() {}, setPointerCapture() {}, getContext: () => context,
    getBoundingClientRect: () => ({ left: 0, top: 0, width: 900, height: 360 }) };
  const view = new CompositionCanvas(canvas, nodeCatalog, node => selections.push(node), (...args) => calls.push(args));
  view.update(graphState, 0);
  canvas.onpointerdown({ clientX: 40, clientY: 40, pointerId: 1, button: 0 });
  canvas.onpointermove({ clientX: 90, clientY: 65 });
  assert.equal(calls.length, 0); assert.equal(graphState.nodes[0].x, 0);
  canvas.onpointerup({ clientX: 90, clientY: 65 });
  assert.deepEqual(calls[0], ["node.position", 0, 0, 0, 50, "25"]);
  canvas.onpointerdown({ clientX: 230, clientY: 64, pointerId: 1, button: 0 });
  canvas.onpointermove({ clientX: 260, clientY: 64 });
  canvas.onpointerup({ clientX: 260, clientY: 64 });
  assert.deepEqual(calls[1], ["node.connect", 0, 1, 0, 0]);
  canvas.onpointerdown({ clientX: 260, clientY: 64, pointerId: 1, button: 2 });
  assert.deepEqual(calls[2], ["node.disconnect", 1, 0, 0, 0]);
  canvas.onpointerdown({ clientX: 40, clientY: 40, pointerId: 1, button: 0 });
  canvas.onpointermove({ clientX: 80, clientY: 70 }); canvas.onpointercancel({}); canvas.onpointerup({});
  assert.equal(calls.length, 3); assert.deepEqual(selections, [0, 0, 0]);
  view.fit();
});
test("composition FFI reads native descriptors/state and frees failed previews", () => {
  const heap = new Uint8Array(2 * 1024 * 1024), freed = [], rendered = []; let failed = false;
  const module = { HEAPU8: heap, _malloc: () => 128, _free: p => freed.push(p), cwrap(name) {
    if (name === "jfx_web_session_create") return (_, p) => { new DataView(heap.buffer).setUint32(p, 9, true); return 0; };
    if (["jfx_web_session_destroy", "jfx_web_session_set_effect", "jfx_web_session_render_rgba"].includes(name)) return () => 0;
    if (name === "jfx_node_catalog" || name === "jfx_web_session_graph_state") return (...args) => {
      const p = args.at(-2); heap.set(new TextEncoder().encode(JSON.stringify(name === "jfx_node_catalog" ? nodeCatalog : graphState) + "\0"), p); return 0;
    };
    if (name === "jfx_web_session_render_graph") return (_, node, seconds, w, h, p) => { rendered.push([node, seconds, w, h]); heap.fill(42, p, p + w * h * 4); return failed ? 1 : 0; };
    throw new Error(`unexpected export ${name}`);
  } };
  const bridge = new EmscriptenJoltBridge(module, 1, 1);
  assert.deepEqual(bridge.nodeKinds(), nodeCatalog); assert.deepEqual(bridge.graphState(), graphState);
  const pixels = bridge.renderGraphNode(null, 2, 2, 1).pixels; assert.equal(pixels[0], 42);
  assert.deepEqual(rendered[0], [0xffffffff, 2, 2, 1]);
  assert.throws(() => bridge.renderGraphNode(-1, 0), RangeError);
  assert.throws(() => bridge.renderGraphNode(0, NaN), RangeError);
  assert.throws(() => bridge.renderGraphNode(null, 0, 4097, 1), RangeError);
  failed = true; const before = freed.length; assert.throws(() => bridge.renderGraphNode(1, 0), /Unable to render composition/);
  assert.equal(freed.length, before + 1); assert.equal(pixels[0], 42); bridge.dispose();
});
test("mounted composition inspector filters ports, keeps clip colors and switches previews", () => {
  const previousDocument = globalThis.document;
  globalThis.document = { createElement: tag => new Element(tag), createTextNode: text => ({ textContent: text }) };
  try {
    const calls = [], previews = [], model = structuredClone(graphState);
    const catalog = [ { ...nodeCatalog[0], params: [{ name: "r", label: "Red", min: 0, max: 1, default: 1, integer: false }] }, nodeCatalog[1],
      { name: "image", label: "Image", category: "Source", inputs: [], outputs: [{ name: "out", label: "Image", type: "image" }], params: [], strings: ["Path"] } ];
    model.nodes[0].values = [1];
    const sequence = { width: 1, height: 1, fpsNum: 30, fpsDen: 1, duration: 60, canUndo: false, canRedo: false, tracks: [] };
    const root = new Element("main"), editor = new JoltEditor(root, {
      nodeKinds: () => catalog, graphState: () => structuredClone(model), sequenceState: () => sequence,
      saveDocument: () => "graph\nnode solid\n", sequenceDocument: () => "track V1\nclip solid 0 60\neffect grade_primary 1\n",
      colorOperators: () => [{ name: "grade_primary", label: "Primary", section: "grade", params: [{ name: "exposure", label: "Exposure", min: -10, max: 10, default: 0 }] }],
      renderFrame: () => { previews.push("output"); return bridge.renderFrame(); },
      renderGraphNode: node => { previews.push(node); return bridge.renderFrame(); },
      edit(op, a = 0, b = 0, c = 0, value = 0, text = "") {
        calls.push([op, a, b, c, value, text]);
        if (op === "graph") model.active = true;
        if (op === "node.add") model.nodes.push({ kind: text, label: text, x: 480, y: 0, values: [], strings: [""], inputs: text === "solid" ? [null] : [] });
        if (op === "node.param") model.nodes[a].values[0] = value;
        if (op === "node.connect") model.nodes[b].inputs[c] = { source: a, port: value };
        if (op === "node.disconnect") model.nodes[a].inputs[b] = null;
        if (op === "node.path") model.nodes[a].strings[b] = text;
      },
    });
    const section = title => root.children.find(s => s.children?.[0]?.textContent === title);
    const nodes = section("Node Compositing"), inspector = () => nodes.children.at(-1);
    const field = (parent, prefix) => parent.children.find(e => e.tag === "label" && e.children[0]?.textContent.startsWith(prefix)).children[1];
    const button = title => nodes.children.find(e => e.tag === "button" && e.textContent === title);
    const red = field(inspector(), "Red"); assert.equal(red.min, "0"); assert.equal(red.max, "1"); red.value = "0.2"; red.onchange();
    assert.deepEqual(calls.at(-1), ["node.param", 0, 0, 0, .2, "r"]);
    const selected = field(nodes, "Selected node"); selected.value = "1"; root.handlers.change({ target: selected });
    const input = field(inspector(), "Color ("); assert.deepEqual(input.options.map(o => o.value), ["", "0:0"]);
    input.value = "0:0"; input.onchange(); assert.deepEqual(calls.at(-1), ["node.connect", 0, 1, 0, 0, ""]);
    const disconnect = field(inspector(), "Color ("); disconnect.value = ""; disconnect.onchange(); assert.equal(calls.at(-1)[0], "node.disconnect");
    button("Preview selected node").onclick(); assert.equal(previews.at(-1), 1);
    button("Preview graph").onclick(); assert.equal(previews.at(-1), "output");
    const search = field(nodes, "Find operator"); search.value = "image"; search.oninput();
    assert.deepEqual(field(nodes, "Node kind").options.map(o => o.value), ["image"]);
    button("Add node").onclick(); assert.equal(selected.value, "2");
    const path = field(inspector(), "Path"); path.value = "/image with spaces.png";
    inspector().children.find(e => e.textContent === "Apply Path").onclick(); assert.deepEqual(calls.at(-1), ["node.path", 2, 0, 0, 0, "/image with spaces.png"]);
    const grading = section("Color Grading"), colorControls = grading.children.at(-1);
    assert.equal(field(colorControls, "Exposure").value, "1", "sequence controls must stay populated while graph is active");
    field(colorControls, "Exposure").value = "2"; field(colorControls, "Exposure").onchange();
    assert.deepEqual(calls.at(-1), ["grade.param", 0, 0, 0, 2, "exposure"]);
    editor.dispose();
  } finally { globalThis.document = previousDocument; }
});
