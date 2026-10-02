import { JoltPlayer, type JoltWasmBridge } from "./player.js";
import { JoltEditor, type EditorBridge } from "./editor.js";
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

export { JoltEditor } from "./editor.js";
export { NLETimeline, type SequenceState, type SequenceClip } from "./nle.js";
export { CompositionCanvas, type GraphState, type NodeKind, type GraphNode, type NodePort } from "./composition.js";

export function mountJoltEditor(bridge: EditorBridge, rootId = "jolt-editor"): JoltEditor {
  const root = document.getElementById(rootId);
  if (!root) throw new Error(`editor #${rootId} was not found`);
  return new JoltEditor(root, bridge);
}
