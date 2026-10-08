/*
 * Bridge for the C exports in jfx_wasm.c. The beta transport is a UTF-8 JSON
 * envelope ({"effect":"brightness","parameter":0.1}); it keeps browser
 * loading, parsing, and signature verification separate from Core execution.
 */
export class EmscriptenJoltBridge {
    module;
    width;
    height;
    create;
    destroy;
    setEffect;
    renderRgba;
    session;
    disposed = false;
    audioMixer = 0;
    audioRate = 0;
    exportNumber = 0;
    exports = new Set();
    constructor(module, width = 640, height = 360) {
        this.module = module;
        this.width = width;
        this.height = height;
        if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0 || width > 4096 || height > 4096) {
            throw new RangeError("WASM output dimensions must be positive integers");
        }
        this.create = module.cwrap("jfx_web_session_create", "number", ["string", "number"]);
        this.destroy = module.cwrap("jfx_web_session_destroy", null, ["number"]);
        this.setEffect = module.cwrap("jfx_web_session_set_effect", "number", ["number", "string", "number"]);
        this.renderRgba = module.cwrap("jfx_web_session_render_rgba", "number", ["number", "number", "number", "number", "number", "number"]);
        const outPointer = module._jfx_web_alloc(4);
        if (!outPointer)
            throw new Error("WASM heap allocation failed");
        try {
            const result = this.create("webgpu", outPointer);
            if (result !== 0)
                throw new Error(`unable to create the JoltFX WASM session (${result})`);
            this.session = new DataView(module.HEAPU8.buffer).getUint32(outPointer, true);
        }
        finally {
            module._jfx_web_free(outPointer);
        }
        if (!this.session)
            throw new Error("WASM session creation returned a null handle");
    }
    ensureOpen() {
        if (this.disposed)
            throw new Error("WASM session is closed");
    }
    loadDocument(text) {
        this.ensureOpen();
        const bytes = new TextEncoder().encode(text);
        if (!bytes.length || bytes.length > 8 * 1024 * 1024)
            throw new RangeError("Project exceeds 8 MiB");
        const load = this.module.cwrap("jfx_web_session_load_document", "number", ["number", "string", "number", "number", "number"]);
        const result = load(this.session, text, bytes.length, 0, 0);
        if (result !== 0)
            throw new Error(`Project rejected (${result})`);
        this.resetAudio();
    }
    saveDocument() {
        return this.saveText("jfx_web_session_save_document");
    }
    sequenceDocument() { return this.saveText("jfx_web_session_save_sequence"); }
    saveText(name) {
        this.ensureOpen();
        const save = this.module.cwrap(name, "number", ["number", "number", "number", "number"]);
        const capacity = 8 * 1024 * 1024;
        const pointer = this.module._jfx_web_alloc(capacity + 4);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        try {
            const result = save(this.session, pointer, capacity, pointer + capacity);
            if (result !== 0)
                throw new Error(`Unable to save project (${result})`);
            const length = new DataView(this.module.HEAPU8.buffer).getUint32(pointer + capacity, true);
            if (length > capacity)
                throw new Error("Native document exceeds its buffer");
            return new TextDecoder().decode(this.module.HEAPU8.subarray(pointer, pointer + length));
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
    }
    edit(op, a = 0, b = 0, c = 0, value = 0, text = "") {
        this.ensureOpen();
        if (![a, b, c].every(index => Number.isInteger(index) && index >= 0 && index <= 0xffffffff) || !Number.isFinite(value))
            throw new RangeError("Edit indices must be nonnegative integers and the value must be finite");
        const command = this.module.cwrap("jfx_web_session_edit", "number", ["number", "string", "number", "number", "number", "number", "string"]);
        const result = command(this.session, op, a, b, c, value, text);
        if (result !== 0)
            throw new Error(`Edit rejected (${result})`);
        this.resetAudio();
    }
    colorOperators() {
        return this.jsonExport("jfx_color_catalog", false);
    }
    sequenceState() {
        return this.jsonExport("jfx_web_session_sequence_state", true, 4 * 1024 * 1024);
    }
    jsonExport(name, session, capacity = 1024 * 1024) {
        this.ensureOpen();
        const pointer = this.module._jfx_web_alloc(capacity);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        try {
            const call = this.module.cwrap(name, "number", session ? ["number", "number", "number"] : ["number", "number"]);
            if ((session ? call(this.session, pointer, capacity) : call(pointer, capacity)) !== 0)
                throw new Error(`Unable to read ${name}`);
            const end = this.module.HEAPU8.subarray(pointer, pointer + capacity).indexOf(0);
            if (end < 0)
                throw new Error("Native JSON is not terminated");
            return JSON.parse(new TextDecoder().decode(this.module.HEAPU8.subarray(pointer, pointer + end)));
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
    }
    nodeKinds() { return this.jsonExport("jfx_node_catalog", false); }
    graphState() { return this.jsonExport("jfx_web_session_graph_state", true); }
    renderGraphNode(node, seconds, width = this.width, height = this.height) {
        this.ensureOpen();
        if (node !== null && (!Number.isInteger(node) || node < 0 || node >= 256) || !Number.isFinite(seconds) || seconds < 0 || seconds > 1e9 ||
            ![width, height].every(n => Number.isInteger(n) && n > 0 && n <= 4096))
            throw new RangeError("Invalid graph preview node, time or raster");
        const length = width * height * 4, pointer = this.module._jfx_web_alloc(length);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        try {
            const render = this.module.cwrap("jfx_web_session_render_graph", "number", ["number", "number", "number", "number", "number", "number", "number"]);
            if (render(this.session, node ?? 0xffffffff, seconds, width, height, pointer, length) !== 0)
                throw new Error("Unable to render composition");
            return { width, height, pixels: new Uint8ClampedArray(this.module.HEAPU8.slice(pointer, pointer + length)) };
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
    }
    importAsset(name, bytes) {
        this.ensureOpen();
        if (!this.module.FS)
            throw new Error("This WASM build has no virtual filesystem");
        if (bytes.length > 64 * 1024 * 1024)
            throw new RangeError("Asset exceeds 64 MiB");
        const path = "/" + name.replace(/[^a-zA-Z0-9._-]/g, "_");
        this.module.FS.writeFile(path, bytes);
        this.resetAudio();
        return path;
    }
    async loadPackage(bytes) {
        this.ensureOpen();
        const document = new TextDecoder().decode(bytes);
        if (!document.trimStart().startsWith("{")) {
            this.loadDocument(document);
            return;
        }
        let manifest;
        try {
            manifest = JSON.parse(new TextDecoder().decode(bytes));
        }
        catch {
            throw new Error("the beta web transport must be a UTF-8 JSON effect envelope");
        }
        if (!this.isEffectPackage(manifest))
            throw new Error("invalid beta web effect envelope");
        const result = this.setEffect(this.session, manifest.effect, manifest.parameter ?? 0);
        if (result !== 0)
            throw new Error(`Core rejected effect '${manifest.effect}' (${result})`);
    }
    renderFrame(timeSeconds) {
        this.ensureOpen();
        if (!Number.isFinite(timeSeconds) || timeSeconds < 0)
            throw new RangeError("invalid render time");
        const byteLength = this.width * this.height * 4;
        const pointer = this.module._jfx_web_alloc(byteLength);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        try {
            const result = this.renderRgba(this.session, timeSeconds, this.width, this.height, pointer, byteLength);
            if (result !== 0)
                throw new Error(`Core frame render failed (${result})`);
            return { width: this.width, height: this.height,
                pixels: new Uint8ClampedArray(this.module.HEAPU8.slice(pointer, pointer + byteLength)) };
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
    }
    renderSequenceFrame(frame) {
        this.ensureOpen();
        if (!Number.isInteger(frame) || frame < 0 || frame > 0xffffffff)
            throw new RangeError("Invalid sequence frame");
        const render = this.module.cwrap("jfx_web_session_render_frame", "number", ["number", "number", "number", "number", "number", "number"]);
        const length = this.width * this.height * 4, pointer = this.module._jfx_web_alloc(length);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        try {
            if (render(this.session, frame, this.width, this.height, pointer, length) !== 0)
                throw new Error("Unable to render sequence frame");
            return { width: this.width, height: this.height, pixels: new Uint8ClampedArray(this.module.HEAPU8.slice(pointer, pointer + length)) };
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
    }
    renderAudio(sample, frames, rate = 48000) {
        this.ensureOpen();
        if (!Number.isSafeInteger(sample) || sample < 0 || sample > 1e12 || !Number.isInteger(frames) || frames < 1 || frames > 65536 ||
            !Number.isInteger(rate) || rate < 8000 || rate > 192000)
            throw new RangeError("Invalid audio sample range");
        if (this.audioRate !== rate)
            this.resetAudio();
        const pointer = this.module._jfx_web_alloc(frames * 8 + 4);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        try {
            if (!this.audioMixer) {
                const create = this.module.cwrap("jfx_web_session_audio_mixer", "number", ["number", "number", "number"]);
                if (create(this.session, rate, pointer) !== 0)
                    throw new Error("Unable to create audio mixer");
                this.audioMixer = new DataView(this.module.HEAPU8.buffer).getUint32(pointer, true);
                this.audioRate = rate;
            }
            const mix = this.module.cwrap("jfx_web_audio_mixer_render", "number", ["number", "number", "number", "number", "number"]);
            const result = mix(this.audioMixer, sample, frames, pointer, frames * 2);
            if (result !== 0)
                throw new Error(`Audio mix failed (${result})`);
            return new Float32Array(this.module.HEAPU8.slice(pointer, pointer + frames * 8).buffer);
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
    }
    resetAudio() {
        if (this.audioMixer)
            this.module.cwrap("jfx_audio_mixer_destroy", null, ["number"])(this.audioMixer);
        this.audioMixer = this.audioRate = 0;
    }
    beginVideoExport(name, start = 0, frames = 0, audio = true, codec = "") {
        this.ensureOpen();
        if (![start, frames].every(n => Number.isInteger(n) && n >= 0 && n <= 0xffffffff) || !/\.(mp4|mov|mkv|webm)$/.test(name))
            throw new RangeError("Invalid export name or frame range");
        const fs = this.module.FS;
        if (!fs?.readFile || !fs.unlink)
            throw new Error("This WASM build has no export filesystem");
        const path = `/jfx_export_${this.session}_${++this.exportNumber}.${name.split(".").pop()}`;
        const pointer = this.module._jfx_web_alloc(4);
        if (!pointer)
            throw new Error("WASM heap allocation failed");
        let job;
        try {
            const begin = this.module.cwrap("jfx_web_session_export_begin", "number", ["number", "string", "string", "number", "number", "number", "number"]);
            const result = begin(this.session, path, codec, start, frames, audio ? 1 : 0, pointer);
            if (result !== 0)
                throw new Error(result === -9 ? "Encoder unavailable in this build; enable bundled FFmpeg" : `Export rejected (${result})`);
            job = new DataView(this.module.HEAPU8.buffer).getUint32(pointer, true);
        }
        finally {
            this.module._jfx_web_free(pointer);
        }
        const step = this.module.cwrap("jfx_export_step", "number", ["number", "number"]);
        const state = this.module.cwrap("jfx_export_state", "number", ["number"]);
        const completed = this.module.cwrap("jfx_web_export_completed", "number", ["number"]);
        let disposed = false;
        const handle = {
            step: () => {
                if (disposed)
                    throw new Error("Export is closed");
                const result = step(job, 1);
                if (result !== 0)
                    throw new Error(`Export failed (${result})`);
                const done = state(job) === 1;
                return { completed: completed(job), done, bytes: done ? fs.readFile(path).slice() : undefined };
            },
            cancel: () => { if (!disposed)
                this.module.cwrap("jfx_export_cancel", null, ["number"])(job); },
            dispose: () => {
                if (disposed)
                    return;
                disposed = true;
                this.module.cwrap("jfx_export_destroy", null, ["number"])(job);
                try {
                    fs.unlink(path);
                }
                catch { /* cancelled jobs never publish a target */ }
                this.exports.delete(handle);
            },
        };
        this.exports.add(handle);
        return handle;
    }
    dispose() {
        if (this.disposed)
            return;
        this.disposed = true;
        for (const job of this.exports)
            job.dispose();
        this.resetAudio();
        this.destroy(this.session);
    }
    isEffectPackage(value) {
        if (typeof value !== "object" || value === null || !("effect" in value))
            return false;
        const candidate = value;
        return typeof candidate.effect === "string" && candidate.effect.length > 0 &&
            (candidate.parameter === undefined || typeof candidate.parameter === "number" &&
                Number.isFinite(candidate.parameter));
    }
}
