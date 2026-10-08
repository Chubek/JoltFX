import type { JoltFrame } from "./player.js";
import { type SequenceState } from "./nle.js";
import { type GraphState, type NodeKind } from "./composition.js";
export interface ColorOperator {
    name: string;
    label: string;
    section: "grade" | "calibration";
    path: boolean;
    params: {
        name: string;
        label: string;
        min: number;
        max: number;
        default: number;
        integer: boolean;
    }[];
}
export interface EditorBridge {
    edit(op: string, a?: number, b?: number, c?: number, value?: number, text?: string): void;
    loadDocument(text: string): void;
    saveDocument(): string;
    sequenceDocument(): string;
    renderFrame(seconds: number): JoltFrame;
    importAsset(name: string, bytes: Uint8Array): string;
    readAsset?(path: string): Uint8Array;
    colorOperators(): ColorOperator[];
    sequenceState(): SequenceState;
    renderSequenceFrame(frame: number): JoltFrame;
    nodeKinds(): NodeKind[];
    graphState(): GraphState;
    scene3dState?(): Scene3DState;
    renderGraphNode(node: number | null, seconds: number, width?: number, height?: number): JoltFrame;
    renderAudio?(sample: number, frames: number, rate?: number): Float32Array;
    beginVideoExport?(name: string, start?: number, frames?: number, audio?: boolean, codec?: string): VideoExport;
}
export interface Scene3DState {
    active: boolean;
    fps: number;
    frames: number;
    camera: number[];
    undo: boolean;
    redo: boolean;
    objects: {
        id: number;
        name: string;
        visible: boolean;
        mass: number;
        vertices: number;
        triangles: number;
        transform: number[];
        color: number[];
        keys: {
            channel: number;
            frame: number;
            value: number;
            interpolation: number;
        }[];
    }[];
}
export interface VideoExport {
    step(): {
        completed: number;
        done: boolean;
        bytes?: Uint8Array;
    };
    cancel(): void;
    dispose(): void;
}
/** Read the shared sequence format, retaining section-local selection and bypass. */
export declare function colorLayers(document: string, selectedTrack: number, selectedClip: number, operators: ColorOperator[]): {
    op: ColorOperator;
    words: string[];
    enabled: boolean;
}[];
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
    private loop;
    private nle;
    private composition;
    private graphState?;
    private scene3d?;
    private sceneObject;
    private refreshScene;
    private previewNode;
    private refreshGraph;
    private sequence?;
    private refreshNLE;
    private document;
    private readonly refreshColors;
    private audioContext?;
    private readonly audioSources;
    private audioSample;
    private audioWhen;
    private exportJob?;
    private exportRequest;
    private zoom;
    private readonly listener;
    private readonly shortcuts;
    constructor(root: HTMLElement, bridge: EditorBridge);
    setZoom(zoom: number): void;
    private panel;
    private modeling3dPanel;
    private colorPanel;
    private number;
    private text;
    private select;
    private field;
    private button;
    private assetPicker;
    private perform;
    private duplicateNode;
    private fail;
    private currentFrame;
    private render;
    private downloadPPM;
    private tick;
    private stopAudio;
    private scheduleAudio;
    private startExport;
    private download;
    dispose(): void;
}
