export type PlayerEvent = "frame" | "end" | "state";

export interface JoltFrame {
  width: number;
  height: number;
  pixels: Uint8ClampedArray;
}

/* Implemented by the Emscripten/WASM binding shipped with a web release. */
export interface JoltWasmBridge {
  loadPackage(bytes: Uint8Array): Promise<void> | void;
  renderFrame(timeSeconds: number): JoltFrame;
}

type Listener = (value: number) => void;

const MAX_PACKAGE_BYTES = 256 * 1024 * 1024;

export class JoltPlayer {
  private readonly listeners = new Map<PlayerEvent, Set<Listener>>();
  private animationHandle: number | undefined;
  private previousTimestamp: number | undefined;
  private _duration = 0;
  private _time = 0;
  private _loop = false;
  private _playing = false;

  public constructor(
    private readonly canvas: HTMLCanvasElement,
    private readonly bridge: JoltWasmBridge,
  ) {}

  public get playing(): boolean { return this._playing; }
  public get time(): number { return this._time; }
  public get duration(): number { return this._duration; }

  public async load(source: string | Blob | Uint8Array, durationSeconds: number): Promise<void> {
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

  public play(): void {
    if (this._duration <= 0) throw new Error("load a package before playback");
    if (this._playing) return;
    this._playing = true;
    this.previousTimestamp = undefined;
    this.requestFrame();
    this.emit("state", this._time);
  }

  public pause(): void {
    this._playing = false;
    if (this.animationHandle !== undefined) {
      cancelAnimationFrame(this.animationHandle);
      this.animationHandle = undefined;
    }
    this.emit("state", this._time);
  }

  public seek(timeSeconds: number): void {
    if (!Number.isFinite(timeSeconds) || timeSeconds < 0 || timeSeconds > this._duration) {
      throw new RangeError("seek time is outside the loaded timeline");
    }
    this._time = timeSeconds;
    this.render();
    this.emit("state", this._time);
  }

  public setLoop(loop: boolean): void { this._loop = loop; }

  public on(event: PlayerEvent, listener: Listener): () => void {
    const listeners = this.listeners.get(event) ?? new Set<Listener>();
    listeners.add(listener);
    this.listeners.set(event, listeners);
    return () => listeners.delete(listener);
  }

  public bindDropTarget(target: HTMLElement): void {
    target.addEventListener("dragover", (event) => event.preventDefault());
    target.addEventListener("drop", (event) => {
      event.preventDefault();
      const file = event.dataTransfer?.files.item(0);
      if (file) void this.load(file, this._duration || 1);
    });
  }

  /* Useful for host integrations and deterministic tests without RAF. */
  public advance(elapsedSeconds: number): void {
    if (!Number.isFinite(elapsedSeconds) || elapsedSeconds < 0) {
      throw new RangeError("elapsedSeconds must be finite and non-negative");
    }
    if (!this._playing) return;
    this._time += elapsedSeconds;
    if (this._time >= this._duration) {
      if (this._loop) this._time %= this._duration;
      else {
        this._time = this._duration;
        this.pause();
        this.emit("end", this._time);
      }
    }
    this.render();
    this.emit("frame", this._time);
  }

  private requestFrame(): void {
    this.animationHandle = requestAnimationFrame((timestamp) => {
      const previous = this.previousTimestamp ?? timestamp;
      this.previousTimestamp = timestamp;
      this.advance((timestamp - previous) / 1000);
      if (this._playing) this.requestFrame();
    });
  }

  private render(): void {
    const frame = this.bridge.renderFrame(this._time);
    if (!frame.width || !frame.height || frame.pixels.length !== frame.width * frame.height * 4) {
      throw new Error("WASM bridge returned an invalid RGBA frame");
    }
    this.canvas.width = frame.width;
    this.canvas.height = frame.height;
    const context = this.canvas.getContext("2d");
    if (!context) throw new Error("2D canvas output is unavailable");
    /* Copy into an ArrayBuffer-backed view: ImageData deliberately rejects a
       SharedArrayBuffer view in current browser TypeScript declarations. */
    const pixels = new Uint8ClampedArray(frame.pixels.length);
    pixels.set(frame.pixels);
    context.putImageData(new ImageData(pixels, frame.width, frame.height), 0, 0);
  }

  private emit(event: PlayerEvent, value: number): void {
    for (const listener of this.listeners.get(event) ?? []) listener(value);
  }

  private async readSource(source: string | Blob | Uint8Array): Promise<Uint8Array> {
    if (source instanceof Uint8Array) return source;
    if (typeof source !== "string") return new Uint8Array(await source.arrayBuffer());
    const base = globalThis.location?.href;
    const url = new URL(source, base);
    if (globalThis.location?.origin && url.origin !== globalThis.location.origin) {
      throw new Error("cross-origin packages require an explicit same-origin proxy");
    }
    const response = await fetch(url);
    if (!response.ok) throw new Error(`package request failed (${response.status})`);
    return new Uint8Array(await response.arrayBuffer());
  }
}
