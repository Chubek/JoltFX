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
    constructor(module, width = 640, height = 360) {
        this.module = module;
        this.width = width;
        this.height = height;
        if (!Number.isInteger(width) || !Number.isInteger(height) || width <= 0 || height <= 0) {
            throw new RangeError("WASM output dimensions must be positive integers");
        }
        this.create = module.cwrap("jfx_web_session_create", "number", ["string", "number"]);
        this.destroy = module.cwrap("jfx_web_session_destroy", null, ["number"]);
        this.setEffect = module.cwrap("jfx_web_session_set_effect", "number", ["number", "string", "number"]);
        this.renderRgba = module.cwrap("jfx_web_session_render_rgba", "number", ["number", "number", "number", "number", "number", "number"]);
        const outPointer = module._malloc(4);
        try {
            const result = this.create("webgpu", outPointer);
            if (result !== 0)
                throw new Error(`unable to create the JoltFX WASM session (${result})`);
            this.session = new DataView(module.HEAPU8.buffer).getUint32(outPointer, true);
        }
        finally {
            module._free(outPointer);
        }
        if (!this.session)
            throw new Error("WASM session creation returned a null handle");
    }
    async loadPackage(bytes) {
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
        if (!Number.isFinite(timeSeconds) || timeSeconds < 0)
            throw new RangeError("invalid render time");
        const byteLength = this.width * this.height * 4;
        const pointer = this.module._malloc(byteLength);
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
            this.module._free(pointer);
        }
    }
    dispose() { this.destroy(this.session); }
    isEffectPackage(value) {
        if (typeof value !== "object" || value === null || !("effect" in value))
            return false;
        const candidate = value;
        return typeof candidate.effect === "string" && candidate.effect.length > 0 &&
            (candidate.parameter === undefined || typeof candidate.parameter === "number" &&
                Number.isFinite(candidate.parameter));
    }
}
