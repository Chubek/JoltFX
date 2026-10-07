import type { JoltFrame } from "./player.js";
import { NLETimeline, type SequenceState } from "./nle.js";
import { CompositionCanvas, type GraphState, type NodeKind } from "./composition.js";

export interface ColorOperator {
  name: string; label: string; section: "grade" | "calibration"; path: boolean;
  params: { name: string; label: string; min: number; max: number; default: number; integer: boolean }[];
}

export interface EditorBridge {
  edit(op: string, a?: number, b?: number, c?: number, value?: number, text?: string): void;
  loadDocument(text: string): void;
  saveDocument(): string;
  sequenceDocument(): string;
  renderFrame(seconds: number): JoltFrame;
  importAsset(name: string, bytes: Uint8Array): string;
  colorOperators(): ColorOperator[];
  sequenceState(): SequenceState;
  renderSequenceFrame(frame: number): JoltFrame;
  nodeKinds(): NodeKind[];
  graphState(): GraphState;
  renderGraphNode(node: number | null, seconds: number, width?: number, height?: number): JoltFrame;
  renderAudio?(sample: number, frames: number, rate?: number): Float32Array;
  beginVideoExport?(name: string, start?: number, frames?: number, audio?: boolean, codec?: string): VideoExport;
}
export interface VideoExport {
  step(): { completed: number; done: boolean; bytes?: Uint8Array };
  cancel(): void;
  dispose(): void;
}

/** Read the shared sequence format, retaining section-local selection and bypass. */
export function colorLayers(document: string, selectedTrack: number, selectedClip: number,
  operators: ColorOperator[]): { op: ColorOperator; words: string[]; enabled: boolean }[] {
  let track = -1, clip = -1, effect = -1;
  const layers: { op: ColorOperator; words: string[]; enabled: boolean; index: number }[] = [];
  for (const line of document.split("\n")) {
    const words = line.trim().match(/"(?:\\.|[^"\\])*"|[^\s]+/g)?.map(word => word.startsWith('"') ? JSON.parse(word) as string : word) ?? [];
    if (words[0] === "track") { track++; clip = -1; }
    if (words[0] === "clip") { clip++; effect = -1; }
    if (words[0] === "effect") {
      effect++;
      if (track === selectedTrack && clip === selectedClip) {
        const op = operators.find(candidate => candidate.name === words[1]);
        if (op) layers.push({ op, words, enabled: true, index: effect });
      }
    }
    if (words[0] === "disable" && track === selectedTrack && +words[1] === selectedClip + 1) {
      const layer = layers.find(candidate => candidate.index === +words[2] - 1);
      if (layer) layer.enabled = false;
    }
  }
  return layers;
}

/** All edits and rendering are native. DOM state contains only selection and the clock. */
export class JoltEditor {
  private time = 0;
  private playing = false;
  private frameRequest = 0;
  private previous = 0;
  private readonly preview = document.createElement("canvas");
  private readonly status = document.createElement("p");
  private readonly timeline = document.createElement("canvas");
  private readonly graph = document.createElement("canvas");
  private readonly frame = this.number(0, 0);
  private readonly track = this.number(0, 0);
  private readonly clip = this.number(0, 0);
  private readonly effect = this.number(0, 0);
  private readonly node = this.number(0, 0);
  private fps = 30;
  private duration = 300;
  private loop = true;
  private nle!: NLETimeline;
  private composition!: CompositionCanvas;
  private graphState?: GraphState;
  private previewNode: number | null = null;
  private refreshGraph: () => void = () => {};
  private sequence?: SequenceState;
  private refreshNLE: () => void = () => {};
  private document = "";
  private readonly refreshColors: (() => void)[] = [];
  private audioContext?: AudioContext;
  private readonly audioSources = new Set<AudioBufferSourceNode>();
  private audioSample = 0;
  private audioWhen = 0;
  private exportJob?: VideoExport;
  private exportRequest = 0;
  private zoom = 1;
  private readonly listener = (event: Event): void => {
    const target = event.target as HTMLInputElement;
    if (target === this.frame) { this.stopAudio(); this.time = Number(this.frame.value) / this.fps; this.render(); }
    if (target === this.track || target === this.clip) { this.refreshNLE(); this.refreshColors.forEach(refresh => refresh()); this.render(); }
    if (target === this.node) {
      if (this.previewNode !== null) this.previewNode=+this.node.value;
      this.refreshGraph(); this.render();
    }
  };
  private readonly shortcuts = (event: KeyboardEvent): void => {
    if ((event.target as HTMLElement).closest("input, select, textarea")) return;
    const modifier = event.ctrlKey || event.metaKey;
    if (modifier && event.key.toLowerCase() === "z") { event.preventDefault(); this.perform(() => this.bridge.edit(event.shiftKey ? "redo" : "undo")); }
    else if (event.key === "Delete" || event.key.toLowerCase() === "s" && !modifier) {
      if (this.graphState?.active) {
        if (event.key === "Delete") { event.preventDefault(); this.perform(() => this.bridge.edit("node.remove", +this.node.value)); }
        return;
      }
      event.preventDefault(); this.perform(() => this.bridge.edit(event.key === "Delete" ? event.shiftKey ? "clip.ripple_delete" : "clip.remove" : "clip.split",
        +this.track.value, +this.clip.value, 0, this.currentFrame()));
    }
    else if (modifier && event.key.toLowerCase() === "d" && this.graphState?.active) { event.preventDefault(); this.perform(() => this.duplicateNode()); }
  };

  constructor(private readonly root: HTMLElement, private readonly bridge: EditorBridge) {
    root.classList.add("jolt-editor");
    root.tabIndex = 0; root.addEventListener("keydown", this.shortcuts);
    root.addEventListener("wheel", (event) => {
      if (!event.ctrlKey && !event.metaKey) return;
      event.preventDefault();
      this.setZoom(this.zoom * Math.exp(-event.deltaY / 500));
    }, { passive: false });
    const toolbar = document.createElement("nav");
    root.append(toolbar, this.preview, this.status);
    toolbar.append(this.button("Play / pause", () => {
      this.playing = !this.playing;
      this.stopAudio();
      if (this.playing && globalThis.AudioContext && this.bridge.renderAudio) {
        this.audioContext ??= new AudioContext(); void this.audioContext.resume().catch(error => this.fail(error));
      }
      this.previous = 0;
      if (this.playing) this.frameRequest = requestAnimationFrame(this.tick);
      else cancelAnimationFrame(this.frameRequest);
    }), this.button("New sequence", () => {
      bridge.edit("sequence.new", 320, 180, 30, 1); this.time = 0;
    }), this.button("Stop", () => { this.playing = false; this.time = 0; cancelAnimationFrame(this.frameRequest); }),
      this.button("Undo", () => bridge.edit("undo")), this.button("Redo", () => bridge.edit("redo")),
      this.button("Toggle loop", () => { this.loop = !this.loop; }), this.button("Save project", () => this.download()));
    const open = document.createElement("input"); open.type = "file"; open.accept = ".jfx";
    open.setAttribute("aria-label", "Open project");
    open.onchange = () => { const f = open.files?.[0]; if (f) void f.text().then(text => this.perform(() => { bridge.loadDocument(text); this.previewNode=null; this.time=0; this.playing=false; cancelAnimationFrame(this.frameRequest); })).catch(e => this.fail(e)); };
    toolbar.append(open);

    const nle = this.panel("NLE timeline");
    const rasterWidth = this.number(320, 1, 4096), rasterHeight = this.number(180, 1, 4096);
    const fpsNumerator = this.number(30, 1), fpsDenominator = this.number(1, 1);
    this.field(nle, "New sequence width", rasterWidth); this.field(nle, "New sequence height", rasterHeight);
    this.field(nle, "FPS numerator", fpsNumerator); this.field(nle, "FPS denominator", fpsDenominator);
    nle.append(this.button("Create empty sequence", () => {
      bridge.edit("sequence.new", +rasterWidth.value, +rasterHeight.value, +fpsNumerator.value, +fpsDenominator.value); this.time = 0;
    }));
    const start = this.number(0, 0), length = this.number(90, 1), media = this.text("");
    const destinationTrack = this.number(0, 0), slip = this.number(0), name = this.text("");
    const source = this.select(["solid", "gradient", "checker", "sweep", "image", "video", "audio"]);
    this.field(nle, "Frame", this.frame); this.field(nle, "Track (0-based)", this.track);
    this.field(nle, "Clip (0-based)", this.clip); this.field(nle, "Start", start); this.field(nle, "Length", length);
    this.field(nle, "Source", source); this.field(nle, "Media path", media);
    this.field(nle, "Destination track", destinationTrack); this.field(nle, "Slip delta (frames)", slip); this.field(nle, "Name", name);
    nle.append(this.button("Export frame as PPM", () => {
      const result = bridge.renderSequenceFrame(Math.floor(this.time * this.fps + 1e-7));
      const header = new TextEncoder().encode(`P6\n${result.width} ${result.height}\n255\n`);
      const ppm = new Uint8Array(header.length + result.width * result.height * 3); ppm.set(header);
      for (let i = 0; i < result.width * result.height; i++) for (let c = 0; c < 3; c++) ppm[header.length + i * 3 + c] = result.pixels[i * 4 + c];
      const url = URL.createObjectURL(new Blob([ppm], { type: "image/x-portable-pixmap" }));
      const link = document.createElement("a"); link.href = url; link.download = `frame_${Math.round(this.time * this.fps)}.ppm`; link.click(); URL.revokeObjectURL(url);
    }));
    nle.append(this.assetPicker(media), this.button("Preview sequence", () => bridge.edit("sequence")),
      this.button("Add track", () => bridge.edit("track.add", 0, 0, 0, 0, "Video")),
      this.button("Add clip", () => bridge.edit("clip.add", +this.track.value, source.selectedIndex, +start.value, +length.value, media.value)),
      this.button("Trim clip", () => bridge.edit("clip.trim", +this.track.value, +this.clip.value, +start.value, +length.value)),
      this.button("Move clip", () => bridge.edit("clip.move", +this.track.value, +this.clip.value, +destinationTrack.value, +start.value)),
       this.button("Split at playhead", () => bridge.edit("clip.split", +this.track.value, +this.clip.value, 0, this.currentFrame())),
      this.button("Duplicate clip", () => bridge.edit("clip.duplicate", +this.track.value, +this.clip.value, +destinationTrack.value, +start.value)),
      this.button("Slip source", () => bridge.edit("clip.slip", +this.track.value, +this.clip.value, 0, +slip.value)),
      this.button("Rename clip", () => bridge.edit("clip.name", +this.track.value, +this.clip.value, 0, 0, name.value)),
      this.button("Enable clip", () => bridge.edit("clip.enabled", +this.track.value, +this.clip.value, 0, 1)),
      this.button("Disable clip", () => bridge.edit("clip.enabled", +this.track.value, +this.clip.value, 0, 0)),
      this.button("Delete clip", () => bridge.edit("clip.remove", +this.track.value, +this.clip.value)),
      this.button("Ripple delete", () => bridge.edit("clip.ripple_delete", +this.track.value, +this.clip.value)),
      this.button("Insert gap", () => bridge.edit("track.insert_gap", +this.track.value, 0, +start.value, +length.value)),
      this.button("Rename track", () => bridge.edit("track.name", +this.track.value, 0, 0, 0, name.value)),
      this.button("Mute / unmute track", () => bridge.edit("track.mute", +this.track.value, 0, 0, this.sequence?.tracks[+this.track.value]?.muted ? 0 : 1)),
      this.button("Solo / unsolo track", () => bridge.edit("track.solo", +this.track.value, 0, 0, this.sequence?.tracks[+this.track.value]?.solo ? 0 : 1)),
      this.button("Move track", () => bridge.edit("track.move", +this.track.value, 0, 0, +destinationTrack.value)),
      this.button("Remove track", () => bridge.edit("track.remove", +this.track.value)),
      this.button("Fit timeline", () => this.nle.fit()), this.button("Toggle snapping", () => { this.nle.snapping = !this.nle.snapping; }), this.timeline);
    this.nle = new NLETimeline(this.timeline, (track, clip) => {
      this.track.value = String(track); this.clip.value = String(clip); this.refreshNLE(); this.refreshColors.forEach(refresh => refresh());
    }, frame => { this.playing = false; this.stopAudio(); cancelAnimationFrame(this.frameRequest); this.time = frame / this.fps; this.render(); },
    (op, a, b, c, value) => this.perform(() => { bridge.edit(op, a, b, c, value); if (op === "clip.move") { this.track.value = String(c); this.clip.value = String(c === a ? b : bridge.sequenceState().tracks[c].clips.length - 1); } }));
    this.refreshNLE = () => {
      this.sequence = bridge.sequenceState(); this.fps = this.sequence.fpsNum / this.sequence.fpsDen; this.duration = this.sequence.duration;
      const selected = this.sequence.tracks[+this.track.value]?.clips[+this.clip.value];
      if (selected) { start.value = String(selected.start); length.value = String(selected.length); name.value = selected.name; }
      this.nle.update(this.sequence, +this.track.value, +this.clip.value, this.currentFrame());
      const audio = selected?.audio;
      clipGain.value=String(audio?.gain ?? 1); pan.value=String(audio?.pan ?? 0);
      fadeIn.value=String(audio?.fadeIn ?? 0); fadeOut.value=String(audio?.fadeOut ?? 0);
      trackGain.value=String(this.sequence.tracks[+this.track.value]?.audioGain ?? 1);
    };

    const audioPanel = this.panel("Audio mixing"), clipGain = this.number(1,0,16), trackGain = this.number(1,0,16);
    const pan = this.number(0,-1,1), fadeIn = this.number(0,0), fadeOut = this.number(0,0);
    for (const [title,input,op] of [["Clip gain",clipGain,"clip.audio.gain"],["Stereo pan",pan,"clip.audio.pan"],
      ["Fade in (frames)",fadeIn,"clip.audio.fade_in"],["Fade out (frames)",fadeOut,"clip.audio.fade_out"],
      ["Track audio gain",trackGain,"track.audio.gain"]] as const) {
      this.field(audioPanel,title,input); input.onchange=() => this.perform(() => bridge.edit(op,+this.track.value,+this.clip.value,0,+input.value));
    }
    audioPanel.append(this.button("Enable clip audio",() => bridge.edit("clip.audio.enabled",+this.track.value,+this.clip.value,0,1)),
      this.button("Mute clip audio",() => bridge.edit("clip.audio.enabled",+this.track.value,+this.clip.value,0,0)));
    const exportPanel=this.panel("Encoded video export"), exportName=this.text("sequence.mp4"), exportStart=this.number(0,0), exportFrames=this.number(0,0), codec=this.text("");
    this.field(exportPanel,"Video output (.mp4/.mov/.mkv/.webm)",exportName); this.field(exportPanel,"Export start frame",exportStart);
    this.field(exportPanel,"Export frame count (0: full sequence)",exportFrames); this.field(exportPanel,"Video encoder (empty: default)",codec);
    exportPanel.append(this.button("Export video with audio",() => this.startExport(exportName.value,+exportStart.value,+exportFrames.value,true,codec.value)),
      this.button("Export silent video",() => this.startExport(exportName.value,+exportStart.value,+exportFrames.value,false,codec.value)),
      this.button("Cancel video export",() => { this.exportJob?.cancel(); this.exportJob?.dispose(); this.exportJob=undefined; cancelAnimationFrame(this.exportRequest); }));

    const layers = this.panel("Layer Effects");
    const kind = this.select(["invert", "opacity", "posterize", "transform", "luma_key"]);
    const param = this.text("amount"), amount = this.number(0), destination = this.number(0, 0);
    this.field(layers, "Effect", this.effect); this.field(layers, "Operator", kind);
    this.field(layers, "Parameter name", param); this.field(layers, "Value", amount);
    this.field(layers, "Move to", destination);
    const stack = (op: string, value = 0, text = "") => bridge.edit(op, +this.track.value, +this.clip.value, +this.effect.value, value, text);
    layers.append(this.button("Add effect", () => stack("effect.add", 0, kind.value)),
      this.button("Set parameter", () => stack("effect.param", +amount.value, param.value)),
      this.button("Enable", () => stack("effect.enabled", 1)), this.button("Bypass", () => stack("effect.enabled", 0)),
      this.button("Move", () => stack("effect.move", +destination.value)), this.button("Remove", () => stack("effect.remove")));
    const opacity = this.number(1, 0, 1); this.field(layers, "Effect opacity", opacity);
    layers.append(this.button("Set opacity", () => stack("effect.opacity", +opacity.value)));

    const colors = bridge.colorOperators();
    this.colorPanel("Color Calibration", "calibration", colors);
    this.colorPanel("Color Grading", "grade", colors);

    const nodes = this.panel("Node Compositing");
    const catalog = bridge.nodeKinds(), nodeKind = this.select([]), search = this.text("");
    const populate = () => {
      nodeKind.replaceChildren(); const find = search.value.toLowerCase();
      for (const k of catalog) if (`${k.category} ${k.label} ${k.name}`.toLowerCase().includes(find)) {
        const option = document.createElement("option"); option.value = k.name; option.textContent = `${k.category} / ${k.label}`; nodeKind.append(option);
      }
    };
    search.oninput = populate; populate();
    this.field(nodes, "Find operator", search); this.field(nodes, "Node kind", nodeKind); this.field(nodes, "Selected node (0-based)", this.node);
    const graphWidth = this.number(320, 1, 4096), graphHeight = this.number(180, 1, 4096), seconds = this.number(0, 0);
    this.field(nodes, "Composition width", graphWidth); this.field(nodes, "Composition height", graphHeight); this.field(nodes, "Preview seconds", seconds);
    seconds.onchange = () => { this.time = +seconds.value; this.render(); };
    this.node.step="1";
    nodes.append(this.button("Preview graph", () => { bridge.edit("graph"); this.previewNode=null; }),
      this.button("New composition", () => { bridge.edit("graph.new", +graphWidth.value, +graphHeight.value); this.node.value = "0"; this.previewNode = null; this.time = 0; }),
      this.button("Set composition size", () => bridge.edit("graph.size", +graphWidth.value, +graphHeight.value)),
      this.button("Add node", () => { bridge.edit("node.add", 0, 0, 0, 0, nodeKind.value); this.node.value = String(bridge.graphState().nodes.length - 1); }),
      this.button("Set output", () => { bridge.edit("node.output", +this.node.value); bridge.edit("graph"); this.previewNode = null; }),
      this.button("Preview selected node", () => { bridge.edit("graph"); this.previewNode = +this.node.value; }),
      this.button("Duplicate node", () => this.duplicateNode()),
      this.button("Reset node", () => bridge.edit("node.reset", +this.node.value)),
      this.button("Delete node", () => { bridge.edit("node.remove", +this.node.value); this.previewNode = null; }),
      this.button("Fit nodes", () => this.composition.fit()),
      this.button("Export composition as PPM", () => {
        const state = bridge.graphState(); this.downloadPPM(bridge.renderGraphNode(null, this.time, state.width, state.height), "composition.ppm");
      }), this.graph);
    this.composition = new CompositionCanvas(this.graph, catalog, node => { this.node.value = String(node); if (this.previewNode!==null) this.previewNode=node; this.refreshGraph(); this.render(); },
      (op, a, b, c, value, text) => this.perform(() => bridge.edit(op, a, b, c, value, text)));
    const inspector = document.createElement("div"); nodes.append(inspector);
    this.refreshGraph = () => {
      const state = bridge.graphState(); this.graphState = state;
      graphWidth.value=String(state.width); graphHeight.value=String(state.height);
      if (!Number.isInteger(+this.node.value) || +this.node.value<0 || +this.node.value >= state.nodes.length) this.node.value = "0";
      if (this.previewNode !== null && this.previewNode >= state.nodes.length) this.previewNode = null;
      this.composition.update(state, +this.node.value); inspector.replaceChildren();
      const n = +this.node.value, node = state.nodes[n], kind = catalog.find(k => k.name === node?.kind);
      if (!kind || !node) { inspector.textContent = "Add a source to begin composing."; return; }
      const label = this.text(node.label); this.field(inspector, "Node label", label);
      label.onchange = () => this.perform(() => bridge.edit("node.label", n, 0, 0, 0, label.value));
      kind.inputs.forEach((port, p) => {
        const connection = this.select(["Disconnected"]); connection.options[0].value = "";
        state.nodes.forEach((source, s) => { if (s === n) return;
          catalog.find(k => k.name === source.kind)?.outputs.forEach((output, o) => {
            if (output.type !== port.type) return;
            const option = document.createElement("option"); option.value = `${s}:${o}`; option.textContent = `${s}: ${source.label} / ${output.label}`; connection.append(option);
          });
        });
        const edge = node.inputs[p]; connection.value = edge ? `${edge.source}:${edge.port}` : "";
        this.field(inspector, `${port.label} (${port.type}${port.required ? ", required" : ""})`, connection);
        connection.onchange = () => this.perform(() => {
          if (!connection.value) bridge.edit("node.disconnect", n, p);
          else { const [source, output] = connection.value.split(":").map(Number); bridge.edit("node.connect", source, n, p, output); }
        });
      });
      kind.params.forEach((param, p) => {
        const input = this.number(node.values[p], param.min, param.max); input.step = param.integer ? "1" : "any";
        this.field(inspector, param.label, input); input.onchange = () => this.perform(() => bridge.edit("node.param", n, 0, 0, +input.value, param.name));
      });
      kind.strings.forEach((name, s) => {
        const input = this.text(node.strings[s] ?? ""); this.field(inspector, name, input);
        inspector.append(this.assetPicker(input), this.button(`Apply ${name}`, () => bridge.edit("node.path", n, s, 0, 0, input.value)),
          this.button(`Clear ${name}`, () => bridge.edit("node.path", n, s, 0, 0, "")));
      });
    };
    root.addEventListener("change", this.listener);
    this.perform(() => bridge.edit(bridge.graphState().active ? "graph" : "sequence"));
  }

  public setZoom(zoom: number): void {
    if (!Number.isFinite(zoom) || zoom < 0.25 || zoom > 8) throw new RangeError("zoom must be between 0.25 and 8");
    this.zoom = zoom;
    for (const canvas of [this.preview, this.timeline, this.graph]) {
      canvas.style.transformOrigin = "top left";
      canvas.style.transform = `scale(${zoom})`;
    }
  }
  private panel(title: string): HTMLElement {
    const section = document.createElement("section"), heading = document.createElement("h2");
    heading.textContent = title; section.append(heading); this.root.append(section); return section;
  }
  private colorPanel(title: string, section: "grade" | "calibration", catalog: ColorOperator[]): void {
    const panel = this.panel(title), operators = catalog.filter(op => op.section === section);
    const add = this.select(operators.map(op => op.name)), selected = this.select([]);
    for (let i = 0; i < operators.length; ++i) add.options[i].textContent = operators[i].label;
    // Values are stable identifiers even when display labels are translated.
    operators.forEach((op, i) => { add.options[i].value = op.name; });
    add.value = section === "grade" ? "grade_primary" : "calib_white_balance";
    const controls = document.createElement("div");
    const command = (action: string, value = 0, text = "") => this.bridge.edit(`${section}.${action}`,
      +this.track.value, +this.clip.value, action === "add" ? 0 : selected.selectedIndex, value, text);
    this.field(panel, "Add operator", add);
    panel.append(this.button("Add", () => command("add", 0, add.value)));
    this.field(panel, "Color layer", selected);
    panel.append(this.button("Enable", () => command("enabled", 1)), this.button("Bypass", () => command("enabled", 0)),
      this.button("Reset", () => command("reset")), this.button("Remove", () => command("remove")),
      this.button("Move up", () => command("move", selected.selectedIndex - 1)),
      this.button("Move down", () => command("move", selected.selectedIndex + 1)), controls);
    const refresh = () => {
      const layers = colorLayers(this.document, +this.track.value, +this.clip.value, operators);
      const index = Math.max(0, Math.min(selected.selectedIndex, layers.length - 1));
      selected.replaceChildren();
      layers.forEach((layer, i) => { const option = document.createElement("option"); option.textContent = `${i + 1}. ${layer.op.label}${layer.enabled ? "" : " (bypassed)"}`; selected.append(option); });
      selected.selectedIndex = layers.length ? index : -1;
      controls.replaceChildren();
      const layer = layers[index];
      if (!layer) { controls.textContent = "Add a color operator to the selected timeline clip."; return; }
      if (layer.op.path) {
        const path = this.text(layer.words[2] ?? ""); this.field(controls, "LUT file", path);
        controls.append(this.assetPicker(path), this.button("Load LUT", () => command("path", 0, path.value)),
          this.button("Clear LUT", () => command("path", 0, "")));
      }
      layer.op.params.forEach((param, i) => {
        const input = this.number(Number(layer.words[2 + (layer.op.path ? 1 : 0) + i] ?? param.default), param.min, param.max);
        input.step = param.integer ? "1" : "any";
        this.field(controls, param.label, input);
        input.onchange = () => this.perform(() => command("param", +input.value, param.name));
      });
    };
    selected.onchange = refresh;
    this.refreshColors.push(refresh);
  }
  private number(value: number, min?: number, max?: number): HTMLInputElement {
    const input = document.createElement("input"); input.type = "number"; input.value = String(value); input.step = "any";
    if (min !== undefined) input.min = String(min); if (max !== undefined) input.max = String(max); return input;
  }
  private text(value: string): HTMLInputElement { const input = document.createElement("input"); input.value = value; return input; }
  private select(values: string[]): HTMLSelectElement {
    const input = document.createElement("select");
    for (const value of values) { const option = document.createElement("option"); option.textContent = value; input.append(option); } return input;
  }
  private field(parent: HTMLElement, title: string, input: HTMLElement): void {
    const label = document.createElement("label"); label.append(document.createTextNode(title + " "), input); parent.append(label);
  }
  private button(title: string, action: () => void): HTMLButtonElement {
    const button = document.createElement("button"); button.textContent = title; button.onclick = () => this.perform(action); return button;
  }
  private assetPicker(path: HTMLInputElement): HTMLInputElement {
    const file = document.createElement("input"); file.type = "file"; file.setAttribute("aria-label", "Import media or LUT");
    file.onchange = () => {
      const asset = file.files?.[0];
      if (asset) void asset.arrayBuffer().then(bytes => { this.stopAudio(); path.value = this.bridge.importAsset(asset.name, new Uint8Array(bytes)); }).catch(e => this.fail(e));
    }; return file;
  }
  private perform(action: () => void): void {
    this.stopAudio();
    try { action(); this.composition.cancel(); this.document = this.bridge.sequenceDocument(); this.status.textContent = ""; this.refreshNLE(); this.refreshColors.forEach(refresh => refresh()); this.refreshGraph(); this.render(); }
    catch (error) { this.fail(error); }
  }
  private duplicateNode(): void {
    this.bridge.edit("node.duplicate",+this.node.value);
    this.node.value=String(this.bridge.graphState().nodes.length-1);
  }
  private fail(error: unknown): void { this.status.textContent = error instanceof Error ? error.message : String(error); }
  private currentFrame(): number { return Math.floor(this.time * this.fps + 1e-7); }
  private render(): void {
    try {
      const result = this.graphState?.active && this.previewNode !== null ? this.bridge.renderGraphNode(this.previewNode, this.time) : this.bridge.renderFrame(this.time);
      this.preview.width = result.width; this.preview.height = result.height;
      this.preview.getContext("2d")?.putImageData(new ImageData(new Uint8ClampedArray(result.pixels), result.width, result.height), 0, 0);
      this.frame.value = String(this.currentFrame());
      if (this.sequence) this.nle.update(this.sequence, +this.track.value, +this.clip.value, this.currentFrame());
    } catch (error) { this.playing = false; this.stopAudio(); this.fail(error); }
  }
  private downloadPPM(result: JoltFrame, name: string): void {
    const header = new TextEncoder().encode(`P6\n${result.width} ${result.height}\n255\n`);
    const ppm = new Uint8Array(header.length + result.width * result.height * 3); ppm.set(header);
    for (let i = 0; i < result.width * result.height; i++) for (let c = 0; c < 3; c++) ppm[header.length + i * 3 + c] = result.pixels[i * 4 + c];
    const url = URL.createObjectURL(new Blob([ppm], { type: "image/x-portable-pixmap" }));
    const link = document.createElement("a"); link.href = url; link.download = name; link.click(); URL.revokeObjectURL(url);
  }
  private tick = (timestamp: number): void => {
    if (!this.playing) return;
    if (this.previous) this.time += (timestamp - this.previous) / 1000;
    const end = this.graphState?.active ? 10 : this.duration / this.fps;
    if (end <= 0) { this.time = 0; this.playing = false; }
    else if (this.time >= end) {
      if (this.loop) { this.time %= end; this.stopAudio(); }
      else { this.time = this.graphState?.active ? end : Math.max(0, (this.duration - 1) / this.fps); this.playing = false; }
    }
    this.previous = timestamp; this.render();
    if (this.playing) this.scheduleAudio(); else this.stopAudio();
    if (this.playing) this.frameRequest = requestAnimationFrame(this.tick);
  };
  private stopAudio(): void {
    for (const source of this.audioSources) source.stop();
    this.audioSources.clear(); this.audioWhen=0;
  }
  private scheduleAudio(): void {
    const context=this.audioContext;
    if (!context || !this.bridge.renderAudio || this.graphState?.active) return;
    try {
      if (!this.audioWhen || this.audioWhen<context.currentTime) {
        this.audioSample=Math.floor(this.time*context.sampleRate); this.audioWhen=context.currentTime+0.03;
      }
      while (this.audioWhen<context.currentTime+0.2) {
        const count=4096, pcm=this.bridge.renderAudio(this.audioSample,count,context.sampleRate);
        const buffer=context.createBuffer(2,count,context.sampleRate);
        for (let c=0;c<2;++c) { const samples=buffer.getChannelData(c); for (let i=0;i<count;++i) samples[i]=pcm[i*2+c]; }
        const source=context.createBufferSource(); source.buffer=buffer; source.connect(context.destination);
        this.audioSources.add(source); source.onended=() => { this.audioSources.delete(source); source.disconnect(); };
        source.start(this.audioWhen); this.audioSample+=count; this.audioWhen+=count/context.sampleRate;
      }
    } catch (error) { this.playing=false; this.stopAudio(); this.fail(error); }
  }
  private startExport(name: string,start: number,count: number,audio: boolean,codec: string): void {
    if (!this.bridge.beginVideoExport) throw new Error("Encoded export bridge unavailable");
    if (this.exportJob) throw new Error("An export is already running");
    this.exportJob=this.bridge.beginVideoExport(name,start,count || (this.graphState?.active?300:0),audio,codec);
    const step=() => {
      const job=this.exportJob; if (!job) return;
      try {
        const result=job.step(); this.status.textContent=`Exported ${result.completed} frames`;
        if (!result.done) { this.exportRequest=requestAnimationFrame(step); return; }
        const url=URL.createObjectURL(new Blob([new Uint8Array(result.bytes!)],{type:"application/octet-stream"}));
        const link=document.createElement("a"); link.href=url; link.download=name; link.click(); URL.revokeObjectURL(url);
        job.dispose(); this.exportJob=undefined;
      } catch (error) { job.dispose(); this.exportJob=undefined; this.fail(error); }
    };
    this.exportRequest=requestAnimationFrame(step);
  }
  private download(): void {
    const url = URL.createObjectURL(new Blob([this.bridge.saveDocument()], { type: "text/plain" }));
    const link = document.createElement("a"); link.href = url; link.download = "project.jfx"; link.click(); URL.revokeObjectURL(url);
  }
  public dispose(): void {
    this.playing=false; this.stopAudio(); void this.audioContext?.close(); cancelAnimationFrame(this.frameRequest);
    cancelAnimationFrame(this.exportRequest); this.exportJob?.dispose(); this.exportJob=undefined;
    this.root.removeEventListener("change",this.listener); this.root.removeEventListener("keydown",this.shortcuts); this.root.replaceChildren();
  }
}
