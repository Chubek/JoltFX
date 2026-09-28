import type { JoltFrame } from "./player.js";
export interface EditorBridge {
    edit(op: string, a?: number, b?: number, c?: number, value?: number, text?: string): void;
    loadDocument(text: string): void;
    saveDocument(): string;
    renderFrame(seconds: number): JoltFrame;
    importAsset(name: string, bytes: Uint8Array): string;
}
/** All edits and rendering are native. DOM state contains only selection and the clock. */
export declare class JoltEditor {
    private readonly root;
    private readonly bridge;
    private time;
    private playing;
    private frameRequest;
    private previous;
    private readonly preview;
    private readonly status;
    private readonly timeline;
    private readonly graph;
    private readonly frame;
    private readonly track;
    private readonly clip;
    private readonly effect;
    private readonly node;
    private fps;
    private duration;
    private document;
    private readonly listener;
    constructor(root: HTMLElement, bridge: EditorBridge);
    private panel;
    private number;
    private text;
    private select;
    private field;
    private button;
    private assetPicker;
    private perform;
    private fail;
    private render;
    private drawDocuments;
    private tick;
    private download;
    dispose(): void;
}
