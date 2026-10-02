const nodeWidth = 210, portY = 44, portStep = 23;
export function nodeHeight(kind) { return 55 + Math.max(kind.inputs.length, kind.outputs.length) * portStep; }
/** Hit ports before bodies, and last-created nodes before covered nodes. */
export function graphHit(state, catalog, x, y, radius = 10) {
    for (let n = state.nodes.length - 1; n >= 0; n--) {
        const node = state.nodes[n], kind = catalog.find(k => k.name === node.kind);
        if (!kind)
            continue;
        for (const side of ["input", "output"]) {
            const ports = side === "input" ? kind.inputs : kind.outputs, px = node.x + (side === "output" ? nodeWidth : 0);
            for (let p = 0; p < ports.length; p++)
                if (Math.hypot(x - px, y - node.y - portY - p * portStep) <= radius)
                    return { node: n, side, port: p };
        }
        if (x >= node.x && x <= node.x + nodeWidth && y >= node.y && y <= node.y + nodeHeight(kind))
            return { node: n };
    }
    return undefined;
}
/** Graph-space layout stays native. Dragging previews a ghost, then commits one
 * edit on release. The engine validates types/cycles before replacing any edge. */
export class CompositionCanvas {
    canvas;
    catalog;
    select;
    edit;
    state;
    selected = 0;
    zoom = 1;
    pan = { x: 20, y: 20 };
    drag;
    wire;
    panning;
    constructor(canvas, catalog, select, edit) {
        this.canvas = canvas;
        this.catalog = catalog;
        this.select = select;
        this.edit = edit;
        canvas.width = 900;
        canvas.height = 360;
        canvas.style.touchAction = "none";
        canvas.setAttribute("aria-label", "Composition graph: drag nodes and typed ports, middle-drag or drag background to pan, wheel to zoom");
        canvas.oncontextmenu = e => e.preventDefault();
        canvas.onpointerdown = e => {
            if (!this.state)
                return;
            const screen = this.point(e), p = this.world(screen), hit = graphHit(this.state, catalog, p.x, p.y, 10 / this.zoom);
            if (e.button === 2) {
                if (hit?.side === "input")
                    this.edit("node.disconnect", hit.node, hit.port, 0, 0);
                return;
            }
            canvas.setPointerCapture(e.pointerId);
            if (e.button === 1 || !hit || e.altKey) {
                this.panning = screen;
                return;
            }
            this.selected = hit.node;
            this.select(hit.node);
            if (hit.side === "output")
                this.wire = { node: hit.node, port: hit.port, x: p.x, y: p.y };
            else if (!hit.side) {
                const node = this.state.nodes[hit.node];
                this.drag = { node: hit.node, x: p.x, y: p.y, startX: node.x, startY: node.y, atX: node.x, atY: node.y };
            }
            this.draw();
        };
        canvas.onpointermove = e => {
            const screen = this.point(e), p = this.world(screen);
            if (this.panning) {
                this.pan.x += screen.x - this.panning.x;
                this.pan.y += screen.y - this.panning.y;
                this.panning = screen;
            }
            if (this.drag) {
                this.drag.atX = Math.max(-1e6, Math.min(1e6, this.drag.startX + p.x - this.drag.x));
                this.drag.atY = Math.max(-1e6, Math.min(1e6, this.drag.startY + p.y - this.drag.y));
            }
            if (this.wire) {
                this.wire.x = p.x;
                this.wire.y = p.y;
            }
            this.draw();
        };
        canvas.onpointerup = e => {
            const d = this.drag, w = this.wire;
            this.drag = undefined;
            this.wire = undefined;
            this.panning = undefined;
            if (d && (Math.abs(d.atX - d.startX) > .01 || Math.abs(d.atY - d.startY) > .01))
                this.edit("node.position", d.node, 0, 0, d.atX, String(d.atY));
            if (w && this.state) {
                const p = this.world(this.point(e)), hit = graphHit(this.state, catalog, p.x, p.y, 12 / this.zoom);
                if (hit?.side === "input")
                    this.edit("node.connect", w.node, hit.node, hit.port, w.port);
            }
            this.draw();
        };
        canvas.onpointercancel = () => { this.cancel(); this.draw(); };
        canvas.onwheel = e => {
            e.preventDefault();
            const screen = this.point(e), world = this.world(screen);
            this.zoom = Math.max(.1, Math.min(3, this.zoom * Math.exp(-e.deltaY / 300)));
            this.pan = { x: screen.x - world.x * this.zoom, y: screen.y - world.y * this.zoom };
            this.draw();
        };
    }
    cancel() { this.drag = undefined; this.wire = undefined; this.panning = undefined; }
    update(state, selected) { this.state = state; this.selected = selected; this.draw(); }
    fit() {
        if (!this.state?.nodes.length) {
            this.zoom = 1;
            this.pan = { x: 20, y: 20 };
            this.draw();
            return;
        }
        const nodes = this.state.nodes, minX = Math.min(...nodes.map(n => n.x)), minY = Math.min(...nodes.map(n => n.y));
        const maxX = Math.max(...nodes.map(n => n.x + nodeWidth)), maxY = Math.max(...nodes.map(n => n.y + nodeHeight(this.catalog.find(k => k.name === n.kind))));
        this.zoom = Math.max(.1, Math.min(2, 860 / (maxX - minX + 40), 320 / (maxY - minY + 40)));
        this.pan = { x: 20 - minX * this.zoom, y: 20 - minY * this.zoom };
        this.draw();
    }
    point(e) {
        const box = this.canvas.getBoundingClientRect();
        return { x: (e.clientX - box.left) * this.canvas.width / box.width, y: (e.clientY - box.top) * this.canvas.height / box.height };
    }
    world(p) { return { x: (p.x - this.pan.x) / this.zoom, y: (p.y - this.pan.y) / this.zoom }; }
    draw() {
        const state = this.state, ctx = this.canvas.getContext("2d");
        if (!ctx || !state)
            return;
        ctx.fillStyle = "#151c26";
        ctx.fillRect(0, 0, this.canvas.width, this.canvas.height);
        ctx.save();
        ctx.translate(this.pan.x, this.pan.y);
        ctx.scale(this.zoom, this.zoom);
        ctx.font = "12px sans-serif";
        const at = (n, p, output) => {
            const node = state.nodes[n], d = this.drag?.node === n ? this.drag : undefined;
            return { x: (d?.atX ?? node.x) + (output ? nodeWidth : 0), y: (d?.atY ?? node.y) + portY + p * portStep };
        };
        const color = (type) => type === "image" ? "#64c3fa" : type === "color" ? "#e6965a" : "#a0dc82";
        const line = (a, b) => { ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.bezierCurveTo(a.x + 60, a.y, b.x - 60, b.y, b.x, b.y); ctx.stroke(); };
        state.nodes.forEach((node, n) => node.inputs.forEach((edge, p) => {
            if (!edge)
                return;
            ctx.strokeStyle = color(this.catalog.find(k => k.name === node.kind).inputs[p].type);
            ctx.lineWidth = 2;
            line(at(edge.source, edge.port, true), at(n, p, false));
        }));
        state.nodes.forEach((node, n) => {
            const k = this.catalog.find(k => k.name === node.kind);
            if (!k)
                return;
            const d = this.drag?.node === n ? this.drag : undefined, x = d?.atX ?? node.x, y = d?.atY ?? node.y;
            ctx.fillStyle = this.selected === n ? "#286a92" : "#354155";
            ctx.fillRect(x, y, nodeWidth, nodeHeight(k));
            ctx.strokeStyle = state.output === n ? "#facc15" : "#53637a";
            ctx.strokeRect(x, y, nodeWidth, nodeHeight(k));
            ctx.fillStyle = "white";
            ctx.fillText(`${n}: ${node.label}`.slice(0, 28), x + 8, y + 20);
            for (const side of ["input", "output"])
                (side === "input" ? k.inputs : k.outputs).forEach((port, p) => {
                    const pos = at(n, p, side === "output");
                    ctx.fillStyle = color(port.type);
                    ctx.beginPath();
                    ctx.arc(pos.x, pos.y, 5, 0, 2 * Math.PI);
                    ctx.fill();
                    ctx.fillStyle = "white";
                    ctx.textAlign = side === "input" ? "left" : "right";
                    ctx.fillText(port.label, pos.x + (side === "input" ? 10 : -10), pos.y + 4);
                    ctx.textAlign = "left";
                });
        });
        if (this.wire) {
            ctx.strokeStyle = "#facc15";
            line(at(this.wire.node, this.wire.port, true), this.wire);
        }
        ctx.restore();
    }
}
