export interface SequenceClip {
    name: string;
    source: string;
    path: string;
    start: number;
    length: number;
    inPoint: number;
    enabled: boolean;
    opacity: number;
    effects: number;
    audio?: {
        enabled: boolean;
        gain: number;
        pan: number;
        fadeIn: number;
        fadeOut: number;
    };
}
export interface SequenceState {
    width: number;
    height: number;
    fpsNum: number;
    fpsDen: number;
    duration: number;
    canUndo: boolean;
    canRedo: boolean;
    tracks: {
        name: string;
        muted: boolean;
        solo: boolean;
        opacity: number;
        audioGain?: number;
        clips: SequenceClip[];
    }[];
}
/** Last-created overlapping clip is visually on top, matching compositing. */
export declare function timelineHit(state: SequenceState, track: number, frame: number): number;
export declare function snapFrame(frame: number, edges: number[], tolerance: number): number;
/** Selection, ruler scrubbing and ghost drag/edge-trim. One native edit on drop. */
export declare class NLETimeline {
    private readonly canvas;
    private readonly select;
    private readonly seek;
    private readonly edit;
    private state?;
    private selectedTrack;
    private selectedClip;
    private frame;
    private scale;
    private first;
    snapping: boolean;
    private drag?;
    constructor(canvas: HTMLCanvasElement, select: (track: number, clip: number) => void, seek: (frame: number) => void, edit: (op: string, a: number, b: number, c: number, value: number) => void);
    update(state: SequenceState, track: number, clip: number, frame: number): void;
    fit(): void;
    private point;
    private draw;
}
