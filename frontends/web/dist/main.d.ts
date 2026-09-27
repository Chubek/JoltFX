import { JoltPlayer, type JoltWasmBridge } from "./player.js";
export { EmscriptenJoltBridge } from "./emscripten_bridge.js";
declare global {
    interface Window {
        joltWasmBridge?: JoltWasmBridge;
    }
}
export declare function mountJoltPlayer(canvasId?: string): JoltPlayer;
