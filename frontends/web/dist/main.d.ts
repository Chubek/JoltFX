import { JoltPlayer, type JoltWasmBridge } from "./player.js";
import { JoltEditor, type EditorBridge } from "./editor.js";
export { EmscriptenJoltBridge } from "./emscripten_bridge.js";
declare global {
    interface Window {
        joltWasmBridge?: JoltWasmBridge;
    }
}
export declare function mountJoltPlayer(canvasId?: string): JoltPlayer;
export { JoltEditor } from "./editor.js";
export { NLETimeline, type SequenceState, type SequenceClip } from "./nle.js";
export { CompositionCanvas, type GraphState, type NodeKind, type GraphNode, type NodePort } from "./composition.js";
export declare function mountJoltEditor(bridge: EditorBridge, rootId?: string): JoltEditor;
