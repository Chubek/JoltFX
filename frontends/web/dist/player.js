const MAX_PACKAGE_BYTES = 256 * 1024 * 1024;
export class JoltPlayer {
    canvas;
    bridge;
    listeners = new Map();
    animationHandle;
    previousTimestamp;
    _duration = 0;
    _time = 0;
    _loop = false;
    _playing = false;
    _zoom = 1;
    constructor(canvas, bridge) {
        this.canvas = canvas;
        this.bridge = bridge;
        if (this.canvas.addEventListener)
            this.canvas.addEventListener("wheel", (event) => {
                if (!event.ctrlKey && !event.metaKey)
                    return;
                event.preventDefault();
                this.setZoom(this._zoom * Math.exp(-event.deltaY / 500));
            }, { passive: false });
    }
    get playing() { return this._playing; }
    get time() { return this._time; }
    get duration() { return this._duration; }
    get zoom() { return this._zoom; }
    setZoom(zoom) {
        if (!Number.isFinite(zoom) || zoom < 0.25 || zoom > 8)
            throw new RangeError("zoom must be between 0.25 and 8");
        this._zoom = zoom;
        this.canvas.style.transformOrigin = "top left";
        this.canvas.style.transform = `scale(${zoom})`;
        this.emit("state", this._time);
    }
    async load(source, durationSeconds) {
        if (!Number.isFinite(durationSeconds) || durationSeconds <= 0) {
            throw new RangeError("durationSeconds must be a finite positive number");
        }
        const bytes = await this.readSource(source);
        if (bytes.byteLength > MAX_PACKAGE_BYTES) {
            throw new RangeError("Jolt package exceeds the 256 MiB browser-player limit");
        }
        await this.bridge.loadPackage(bytes);
        this._duration = durationSeconds;
        this._time = 0;
        this.render();
        this.emit("state", this._time);
    }
    play() {
        if (this._duration <= 0)
            throw new Error("load a package before playback");
        if (this._playing)
            return;
        this._playing = true;
        this.previousTimestamp = undefined;
        this.requestFrame();
        this.emit("state", this._time);
    }
    pause() {
        this._playing = false;
        if (this.animationHandle !== undefined) {
            cancelAnimationFrame(this.animationHandle);
            this.animationHandle = undefined;
        }
        this.emit("state", this._time);
    }
    seek(timeSeconds) {
        if (!Number.isFinite(timeSeconds) || timeSeconds < 0 || timeSeconds > this._duration) {
            throw new RangeError("seek time is outside the loaded timeline");
        }
        this._time = timeSeconds;
        this.render();
        this.emit("state", this._time);
    }
    setLoop(loop) { this._loop = loop; }
    on(event, listener) {
        const listeners = this.listeners.get(event) ?? new Set();
        listeners.add(listener);
        this.listeners.set(event, listeners);
        return () => listeners.delete(listener);
    }
    bindDropTarget(target) {
        target.addEventListener("dragover", (event) => event.preventDefault());
        target.addEventListener("drop", (event) => {
            event.preventDefault();
            const file = event.dataTransfer?.files.item(0);
            if (file)
                void this.load(file, this._duration || 1);
        });
    }
    /* Useful for host integrations and deterministic tests without RAF. */
    advance(elapsedSeconds) {
        if (!Number.isFinite(elapsedSeconds) || elapsedSeconds < 0) {
            throw new RangeError("elapsedSeconds must be finite and non-negative");
        }
        if (!this._playing)
            return;
        this._time += elapsedSeconds;
        if (this._time >= this._duration) {
            if (this._loop)
                this._time %= this._duration;
            else {
                this._time = this._duration;
                this.pause();
                this.emit("end", this._time);
            }
        }
        this.render();
        this.emit("frame", this._time);
    }
    requestFrame() {
        this.animationHandle = requestAnimationFrame((timestamp) => {
            const previous = this.previousTimestamp ?? timestamp;
            this.previousTimestamp = timestamp;
            this.advance((timestamp - previous) / 1000);
            if (this._playing)
                this.requestFrame();
        });
    }
    render() {
        const frame = this.bridge.renderFrame(this._time);
        if (!frame.width || !frame.height || frame.pixels.length !== frame.width * frame.height * 4) {
            throw new Error("WASM bridge returned an invalid RGBA frame");
        }
        this.canvas.width = frame.width;
        this.canvas.height = frame.height;
        const context = this.canvas.getContext("2d");
        if (!context)
            throw new Error("2D canvas output is unavailable");
        /* Copy into an ArrayBuffer-backed view: ImageData deliberately rejects a
           SharedArrayBuffer view in current browser TypeScript declarations. */
        const pixels = new Uint8ClampedArray(frame.pixels.length);
        pixels.set(frame.pixels);
        context.putImageData(new ImageData(pixels, frame.width, frame.height), 0, 0);
    }
    emit(event, value) {
        for (const listener of this.listeners.get(event) ?? [])
            listener(value);
    }
    async readSource(source) {
        if (source instanceof Uint8Array)
            return source;
        if (typeof source !== "string")
            return new Uint8Array(await source.arrayBuffer());
        const base = globalThis.location?.href;
        const url = new URL(source, base);
        if (globalThis.location?.origin && url.origin !== globalThis.location.origin) {
            throw new Error("cross-origin packages require an explicit same-origin proxy");
        }
        const response = await fetch(url);
        if (!response.ok)
            throw new Error(`package request failed (${response.status})`);
        return new Uint8Array(await response.arrayBuffer());
    }
}
