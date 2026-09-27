import { JoltPlayer } from "./player.js";
export { EmscriptenJoltBridge } from "./emscripten_bridge.js";
export function mountJoltPlayer(canvasId = "jolt-canvas") {
    const canvas = document.getElementById(canvasId);
    if (!(canvas instanceof HTMLCanvasElement))
        throw new Error(`canvas #${canvasId} was not found`);
    if (!window.joltWasmBridge)
        throw new Error("load the JoltFX WASM bridge before mounting the player");
    return new JoltPlayer(canvas, window.joltWasmBridge);
}
