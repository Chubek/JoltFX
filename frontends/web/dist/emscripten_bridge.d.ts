import type { JoltFrame, JoltWasmBridge } from "./player.js";
import type { ColorOperator, VideoExport } from "./editor.js";
import type { SequenceState } from "./nle.js";
import type { GraphState, NodeKind } from "./composition.js";
type NativeFunction = (...args: Array<number | string>) => number;
export interface EmscriptenModule {
    HEAPU8: Uint8Array;
    _malloc(bytes: number): number;
    _free(pointer: number): void;
    FS?: {
        writeFile(path: string, bytes: Uint8Array): void;
        readFile?(path: string): Uint8Array;
        unlink?(path: string): void;
    };
    cwrap(name: string, returnType: "number" | null, argumentTypes: string[]): NativeFunction;
}
export declare class EmscriptenJoltBridge implements JoltWasmBridge {
    private readonly module;
    private readonly width;
    private readonly height;
    private readonly create;
    private readonly destroy;
    private readonly setEffect;
    private readonly renderRgba;
    private readonly session;
    private audioMixer;
    private audioRate;
    private exportNumber;
    private readonly exports;
    constructor(module: EmscriptenModule, width?: number, height?: number);
    loadDocument(text: string): void;
    saveDocument(): string;
    sequenceDocument(): string;
    private saveText;
    edit(op: string, a?: number, b?: number, c?: number, value?: number, text?: string): void;
    colorOperators(): ColorOperator[];
    sequenceState(): SequenceState;
    private jsonExport;
    nodeKinds(): NodeKind[];
    graphState(): GraphState;
    renderGraphNode(node: number | null, seconds: number, width?: number, height?: number): JoltFrame;
    importAsset(name: string, bytes: Uint8Array): string;
    loadPackage(bytes: Uint8Array): Promise<void>;
    renderFrame(timeSeconds: number): JoltFrame;
    renderSequenceFrame(frame: number): JoltFrame;
    renderAudio(sample: number, frames: number, rate?: number): Float32Array;
    private resetAudio;
    beginVideoExport(name: string, start?: number, frames?: number, audio?: boolean, codec?: string): VideoExport;
    dispose(): void;
    private isEffectPackage;
}
export {};
