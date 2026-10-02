import { JoltPlayer } from "./player.js";
import { JoltEditor } from "./editor.js";
export { EmscriptenJoltBridge } from "./emscripten_bridge.js";
export function mountJoltPlayer(canvasId = "jolt-canvas") {
    const canvas = document.getElementById(canvasId);
    if (!(canvas instanceof HTMLCanvasElement))
        throw new Error(`canvas #${canvasId} was not found`);
    if (!window.joltWasmBridge)
        throw new Error("load the JoltFX WASM bridge before mounting the player");
    return new JoltPlayer(canvas, window.joltWasmBridge);
}
export { JoltEditor } from "./editor.js";
export { NLETimeline } from "./nle.js";
export { CompositionCanvas } from "./composition.js";
export function mountJoltEditor(bridge, rootId = "jolt-editor") {
    const root = document.getElementById(rootId);
    if (!root)
        throw new Error(`editor #${rootId} was not found`);
    return new JoltEditor(root, bridge);
}
