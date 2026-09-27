import assert from "node:assert/strict";
import test from "node:test";
import { JoltPlayer } from "../dist/player.js";
import { EmscriptenJoltBridge } from "../dist/emscripten_bridge.js";

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
