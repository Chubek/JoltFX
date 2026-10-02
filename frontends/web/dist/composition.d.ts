export interface NodePort {
    name: string;
    label: string;
    type: "image" | "color" | "float";
    required: boolean;
}
export interface NodeKind {
    name: string;
    label: string;
    category: string;
    inputs: NodePort[];
    outputs: NodePort[];
    params: {
        name: string;
        label: string;
        min: number;
        max: number;
        default: number;
        step: number;
        integer: boolean;
    }[];
    strings: string[];
}
export interface GraphNode {
    kind: string;
    label: string;
    x: number;
    y: number;
    values: number[];
    strings: string[];
    inputs: ({
        source: number;
        port: number;
    } | null)[];
}
export interface GraphState {
    active: boolean;
    width: number;
    height: number;
    output: number | null;
    canUndo: boolean;
    canRedo: boolean;
    nodes: GraphNode[];
}
export type GraphHit = {
    node: number;
    side?: "input" | "output";
    port?: number;
};
export declare function nodeHeight(kind: NodeKind): number;
/** Hit ports before bodies, and last-created nodes before covered nodes. */
export declare function graphHit(state: GraphState, catalog: NodeKind[], x: number, y: number, radius?: number): GraphHit | undefined;
/** Graph-space layout stays native. Dragging previews a ghost, then commits one
 * edit on release. The engine validates types/cycles before replacing any edge. */
export declare class CompositionCanvas {
    private readonly canvas;
    private readonly catalog;
    private readonly select;
    private readonly edit;
    private state?;
    private selected;
    private zoom;
    private pan;
    private drag?;
    private wire?;
    private panning?;
    constructor(canvas: HTMLCanvasElement, catalog: NodeKind[], select: (node: number) => void, edit: (op: string, a: number, b: number, c: number, value: number, text?: string) => void);
    cancel(): void;
    update(state: GraphState, selected: number): void;
    fit(): void;
    private point;
    private world;
    private draw;
}
