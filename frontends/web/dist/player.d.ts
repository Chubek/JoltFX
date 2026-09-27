export type PlayerEvent = "frame" | "end" | "state";
export interface JoltFrame {
    width: number;
    height: number;
    pixels: Uint8ClampedArray;
}
export interface JoltWasmBridge {
    loadPackage(bytes: Uint8Array): Promise<void> | void;
    renderFrame(timeSeconds: number): JoltFrame;
}
type Listener = (value: number) => void;
export declare class JoltPlayer {
    private readonly canvas;
    private readonly bridge;
    private readonly listeners;
    private animationHandle;
    private previousTimestamp;
    private _duration;
    private _time;
    private _loop;
    private _playing;
    constructor(canvas: HTMLCanvasElement, bridge: JoltWasmBridge);
    get playing(): boolean;
    get time(): number;
    get duration(): number;
    load(source: string | Blob | Uint8Array, durationSeconds: number): Promise<void>;
    play(): void;
    pause(): void;
    seek(timeSeconds: number): void;
    setLoop(loop: boolean): void;
    on(event: PlayerEvent, listener: Listener): () => void;
    bindDropTarget(target: HTMLElement): void;
    advance(elapsedSeconds: number): void;
    private requestFrame;
    private render;
    private emit;
    private readSource;
}
export {};
