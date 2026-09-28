import type { JoltFrame, JoltWasmBridge } from "./player.js";
type NativeFunction = (...args: Array<number | string>) => number;
export interface EmscriptenModule {
    HEAPU8: Uint8Array;
    _malloc(bytes: number): number;
    _free(pointer: number): void;
    FS?: {
        writeFile(path: string, bytes: Uint8Array): void;
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
    constructor(module: EmscriptenModule, width?: number, height?: number);
    loadDocument(text: string): void;
    saveDocument(): string;
    edit(op: string, a?: number, b?: number, c?: number, value?: number, text?: string): void;
    importAsset(name: string, bytes: Uint8Array): string;
    loadPackage(bytes: Uint8Array): Promise<void>;
    renderFrame(timeSeconds: number): JoltFrame;
    dispose(): void;
    private isEffectPackage;
}
export {};
