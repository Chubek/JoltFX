/** Last-created overlapping clip is visually on top, matching compositing. */
export function timelineHit(state, track, frame) {
    const clips = state.tracks[track]?.clips ?? [];
    for (let i = clips.length - 1; i >= 0; i--)
        if (frame >= clips[i].start && frame < clips[i].start + clips[i].length)
            return i;
    return -1;
}
export function snapFrame(frame, edges, tolerance) {
    let result = Math.max(0, Math.round(frame)), distance = tolerance;
    for (const edge of edges)
        if (Math.abs(frame - edge) < distance) {
            result = edge;
            distance = Math.abs(frame - edge);
        }
    return result;
}
/** Selection, ruler scrubbing and ghost drag/edge-trim. One native edit on drop. */
export class NLETimeline {
    canvas;
    select;
    seek;
    edit;
    state;
    selectedTrack = 0;
    selectedClip = 0;
    frame = 0;
    scale = 2;
    first = 0;
    snapping = true;
    drag;
    constructor(canvas, select, seek, edit) {
        this.canvas = canvas;
        this.select = select;
        this.seek = seek;
        this.edit = edit;
        canvas.style.touchAction = "none";
        canvas.setAttribute("aria-label", "NLE timeline: select and drag clips, drag edges to trim, click ruler to seek");
        canvas.onpointerdown = event => {
            if (!this.state)
                return;
            const p = this.point(event), frame = Math.max(0, Math.round(this.first + (p.x - 120) / this.scale));
            if (p.y < 28) {
                this.seek(frame);
                return;
            }
            const track = this.state.tracks.length - 1 - Math.floor((p.y - 28) / 36);
            if (track < 0 || track >= this.state.tracks.length)
                return;
            const index = timelineHit(this.state, track, frame);
            this.select(track, Math.max(0, index));
            this.seek(frame);
            if (index < 0 || p.x < 120)
                return;
            const clip = this.state.tracks[track].clips[index];
            const left = 120 + (clip.start - this.first) * this.scale, right = left + clip.length * this.scale;
            const mode = p.x - left < 7 ? "head" : right - p.x < 7 ? "tail" : "move";
            this.drag = { track, clip: index, x: p.x, mode,
                start: clip.start, length: clip.length, at: clip.start + (mode === "tail" ? clip.length : 0), to: track };
            canvas.setPointerCapture(event.pointerId);
        };
        canvas.onpointermove = event => {
            if (!this.drag || !this.state)
                return;
            const p = this.point(event), d = this.drag;
            d.to = Math.max(0, Math.min(this.state.tracks.length - 1, this.state.tracks.length - 1 - Math.floor((p.y - 28) / 36)));
            const at = d.start + (p.x - d.x) / this.scale + (d.mode === "tail" ? d.length : 0);
            const edges = this.state.tracks[d.to].clips.flatMap((clip, index) => d.to === d.track && index === d.clip ? [] : [clip.start, clip.start + clip.length]);
            d.at = snapFrame(at, this.snapping ? [this.frame, ...edges] : [], 8 / this.scale);
            this.draw();
        };
        canvas.onpointerup = () => {
            const d = this.drag;
            this.drag = undefined;
            if (!d)
                return;
            if (d.mode === "move" && (d.at !== d.start || d.to !== d.track))
                this.edit("clip.move", d.track, d.clip, d.to, d.at);
            if (d.mode === "head" && d.at !== d.start)
                this.edit("clip.trim", d.track, d.clip, d.at, d.start + d.length - d.at);
            if (d.mode === "tail" && d.at !== d.start + d.length)
                this.edit("clip.trim", d.track, d.clip, d.start, d.at - d.start);
            this.draw();
        };
        canvas.onpointercancel = () => { this.drag = undefined; this.draw(); };
        canvas.onwheel = event => {
            event.preventDefault();
            if (event.ctrlKey || event.metaKey)
                this.scale = Math.max(0.01, Math.min(40, this.scale * Math.exp(-event.deltaY / 250)));
            else
                this.first = Math.max(0, this.first + (event.deltaX || event.deltaY) / this.scale);
            this.draw();
        };
    }
    update(state, track, clip, frame) {
        this.state = state;
        this.selectedTrack = track;
        this.selectedClip = clip;
        this.frame = frame;
        this.draw();
    }
    fit() { this.first = 0; this.scale = 780 / Math.max(30, this.state?.duration ?? 30); this.draw(); }
    point(event) {
        const box = this.canvas.getBoundingClientRect();
        return { x: (event.clientX - box.left) * this.canvas.width / box.width, y: (event.clientY - box.top) * this.canvas.height / box.height };
    }
    draw() {
        const state = this.state, ctx = this.canvas.getContext("2d");
        if (!ctx || !state)
            return;
        this.canvas.width = 900;
        this.canvas.height = Math.max(64, 28 + state.tracks.length * 36);
        ctx.fillStyle = "#151c26";
        ctx.fillRect(0, 0, 900, this.canvas.height);
        ctx.font = "12px sans-serif";
        ctx.save();
        ctx.beginPath();
        ctx.rect(120, 0, 780, this.canvas.height);
        ctx.clip();
        const step = Math.max(1, Math.ceil(70 / this.scale));
        for (let f = Math.ceil(this.first / step) * step; f < this.first + 780 / this.scale; f += step) {
            const x = 120 + (f - this.first) * this.scale;
            ctx.fillStyle = "#cbd5e1";
            ctx.fillText(String(f), x + 2, 17);
            ctx.fillStyle = "#273141";
            ctx.fillRect(x, 24, 1, this.canvas.height);
        }
        state.tracks.forEach((track, ti) => track.clips.forEach((clip, ci) => {
            const x = 120 + (clip.start - this.first) * this.scale, y = 30 + (state.tracks.length - 1 - ti) * 36;
            ctx.fillStyle = !clip.enabled || track.muted ? "#444" : ti === this.selectedTrack && ci === this.selectedClip ? "#3689b6" : "#285d78";
            ctx.fillRect(x, y, Math.max(1, clip.length * this.scale), 30);
            ctx.save();
            ctx.beginPath();
            ctx.rect(x, y, clip.length * this.scale, 30);
            ctx.clip();
            ctx.fillStyle = "white";
            ctx.fillText(clip.name, x + 4, y + 20);
            ctx.restore();
        }));
        const d = this.drag;
        if (d) {
            const start = d.mode === "head" || d.mode === "move" ? d.at : d.start;
            const end = d.mode === "tail" ? d.at : d.mode === "head" ? d.start + d.length : d.at + d.length;
            ctx.strokeStyle = "#facc15";
            ctx.strokeRect(120 + (start - this.first) * this.scale, 30 + (state.tracks.length - 1 - d.to) * 36, (end - start) * this.scale, 30);
        }
        ctx.fillStyle = "#facc15";
        ctx.fillRect(120 + (this.frame - this.first) * this.scale, 0, 2, this.canvas.height);
        ctx.restore();
        state.tracks.forEach((track, i) => {
            ctx.fillStyle = "white";
            ctx.fillText(`${track.muted ? "M " : ""}${track.solo ? "S " : ""}${i}: ${track.name}`, 5, 50 + (state.tracks.length - 1 - i) * 36);
        });
    }
}
