import { JoltPlayer, type JoltWasmBridge } from "./player.js";
export { EmscriptenJoltBridge } from "./emscripten_bridge.js";

declare global {
  interface Window { joltWasmBridge?: JoltWasmBridge; }
}

export function mountJoltPlayer(canvasId = "jolt-canvas"): JoltPlayer {
  const canvas = document.getElementById(canvasId);
  if (!(canvas instanceof HTMLCanvasElement)) throw new Error(`canvas #${canvasId} was not found`);
  if (!window.joltWasmBridge) throw new Error("load the JoltFX WASM bridge before mounting the player");
  return new JoltPlayer(canvas, window.joltWasmBridge);
}
