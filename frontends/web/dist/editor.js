import { NLETimeline } from "./nle.js";
import { CompositionCanvas } from "./composition.js";
/** Read the shared sequence format, retaining section-local selection and bypass. */
export function colorLayers(document, selectedTrack, selectedClip, operators) {
    let track = -1, clip = -1, effect = -1;
    const layers = [];
    for (const line of document.split("\n")) {
        const words = line.trim().match(/"(?:\\.|[^"\\])*"|[^\s]+/g)?.map(word => word.startsWith('"') ? JSON.parse(word) : word) ?? [];
        if (words[0] === "track") {
            track++;
            clip = -1;
        }
        if (words[0] === "clip") {
            clip++;
            effect = -1;
        }
        if (words[0] === "effect") {
            effect++;
            if (track === selectedTrack && clip === selectedClip) {
                const op = operators.find(candidate => candidate.name === words[1]);
                if (op)
                    layers.push({ op, words, enabled: true, index: effect });
            }
        }
        if (words[0] === "disable" && track === selectedTrack && +words[1] === selectedClip + 1) {
            const layer = layers.find(candidate => candidate.index === +words[2] - 1);
            if (layer)
                layer.enabled = false;
        }
    }
    return layers;
}
/** All edits and rendering are native. DOM state contains only selection and the clock. */
export class JoltEditor {
    root;
    bridge;
    cancelSceneNavigation = () => { };
    time = 0;
    playing = false;
    frameRequest = 0;
    previous = 0;
    preview = document.createElement("canvas");
    status = document.createElement("p");
    timeline = document.createElement("canvas");
    graph = document.createElement("canvas");
    frame = this.number(0, 0);
    track = this.number(0, 0);
    clip = this.number(0, 0);
    effect = this.number(0, 0);
    node = this.number(0, 0);
    fps = 30;
    duration = 300;
    loop = true;
    nle;
    composition;
    graphState;
    scene3d;
    sceneObject = 0;
    refreshScene = () => { };
    previewNode = null;
    refreshGraph = () => { };
    sequence;
    refreshNLE = () => { };
    document = "";
    refreshColors = [];
    audioContext;
    audioSources = new Set();
    audioSample = 0;
    audioWhen = 0;
    exportJob;
    exportRequest = 0;
    zoom = 1;
    listener = (event) => {
        const target = event.target;
        if (target === this.frame) {
            this.stopAudio();
            this.time = Number(this.frame.value) / this.fps;
            this.render();
        }
        if (target === this.track || target === this.clip) {
            this.refreshNLE();
            this.refreshColors.forEach(refresh => refresh());
            this.render();
        }
        if (target === this.node) {
            if (this.previewNode !== null)
                this.previewNode = +this.node.value;
            this.refreshGraph();
            this.render();
        }
    };
    shortcuts = (event) => {
        if (event.key === "Escape") {
            this.cancelSceneNavigation();
            return;
        }
        if (event.target.closest("input, select, textarea"))
            return;
        const modifier = event.ctrlKey || event.metaKey;
        if (modifier && event.key.toLowerCase() === "z") {
            event.preventDefault();
            this.perform(() => this.bridge.edit(event.shiftKey ? "redo" : "undo"));
        }
        else if (event.key === "Delete" || event.key.toLowerCase() === "s" && !modifier) {
            if (this.scene3d?.active) {
                if (event.key === "Delete") {
                    event.preventDefault();
                    this.perform(() => this.bridge.edit("3d.remove", this.sceneObject));
                }
                return;
            }
            if (this.graphState?.active) {
                if (event.key === "Delete") {
                    event.preventDefault();
                    this.perform(() => this.bridge.edit("node.remove", +this.node.value));
                }
                return;
            }
            event.preventDefault();
            this.perform(() => this.bridge.edit(event.key === "Delete" ? event.shiftKey ? "clip.ripple_delete" : "clip.remove" : "clip.split", +this.track.value, +this.clip.value, 0, this.currentFrame()));
        }
        else if (modifier && event.key.toLowerCase() === "d" && this.graphState?.active) {
            event.preventDefault();
            this.perform(() => this.duplicateNode());
        }
    };
    constructor(root, bridge) {
        this.root = root;
        this.bridge = bridge;
        root.classList.add("jolt-editor");
        root.tabIndex = 0;
        root.addEventListener("keydown", this.shortcuts);
        root.addEventListener("wheel", (event) => {
            if (!event.ctrlKey && !event.metaKey)
                return;
            event.preventDefault();
            this.setZoom(this.zoom * Math.exp(-event.deltaY / 500));
        }, { passive: false });
        const toolbar = document.createElement("nav");
        root.append(toolbar, this.preview, this.status);
        toolbar.append(this.button("Play / pause", () => {
            this.playing = !this.playing;
            this.stopAudio();
            if (this.playing && globalThis.AudioContext && this.bridge.renderAudio) {
                this.audioContext ??= new AudioContext();
                void this.audioContext.resume().catch(error => this.fail(error));
            }
            this.previous = 0;
            if (this.playing)
                this.frameRequest = requestAnimationFrame(this.tick);
            else
                cancelAnimationFrame(this.frameRequest);
        }), this.button("New sequence", () => {
            bridge.edit("sequence.new", 320, 180, 30, 1);
            this.time = 0;
        }), this.button("Stop", () => { this.playing = false; this.time = 0; cancelAnimationFrame(this.frameRequest); }), this.button("Undo", () => bridge.edit("undo")), this.button("Redo", () => bridge.edit("redo")), this.button("Toggle loop", () => { this.loop = !this.loop; }), this.button("Save project", () => this.download()));
        const open = document.createElement("input");
        open.type = "file";
        open.accept = ".jfx";
        open.setAttribute("aria-label", "Open project");
        open.onchange = () => { const f = open.files?.[0]; if (f)
            void f.text().then(text => this.perform(() => { bridge.loadDocument(text); this.previewNode = null; this.time = 0; this.playing = false; cancelAnimationFrame(this.frameRequest); })).catch(e => this.fail(e)); };
        toolbar.append(open);
        const nle = this.panel("NLE timeline");
        const rasterWidth = this.number(320, 1, 4096), rasterHeight = this.number(180, 1, 4096);
        const fpsNumerator = this.number(30, 1), fpsDenominator = this.number(1, 1);
        this.field(nle, "New sequence width", rasterWidth);
        this.field(nle, "New sequence height", rasterHeight);
        this.field(nle, "FPS numerator", fpsNumerator);
        this.field(nle, "FPS denominator", fpsDenominator);
        nle.append(this.button("Create empty sequence", () => {
            bridge.edit("sequence.new", +rasterWidth.value, +rasterHeight.value, +fpsNumerator.value, +fpsDenominator.value);
            this.time = 0;
        }));
        const start = this.number(0, 0), length = this.number(90, 1), media = this.text("");
        const destinationTrack = this.number(0, 0), slip = this.number(0), name = this.text("");
        const source = this.select(["solid", "gradient", "checker", "sweep", "image", "video", "audio"]);
        this.field(nle, "Frame", this.frame);
        this.field(nle, "Track (0-based)", this.track);
        this.field(nle, "Clip (0-based)", this.clip);
        this.field(nle, "Start", start);
        this.field(nle, "Length", length);
        this.field(nle, "Source", source);
        this.field(nle, "Media path", media);
        this.field(nle, "Destination track", destinationTrack);
        this.field(nle, "Slip delta (frames)", slip);
        this.field(nle, "Name", name);
        nle.append(this.button("Export frame as PPM", () => {
            const result = bridge.renderSequenceFrame(Math.floor(this.time * this.fps + 1e-7));
            const header = new TextEncoder().encode(`P6\n${result.width} ${result.height}\n255\n`);
            const ppm = new Uint8Array(header.length + result.width * result.height * 3);
            ppm.set(header);
            for (let i = 0; i < result.width * result.height; i++)
                for (let c = 0; c < 3; c++)
                    ppm[header.length + i * 3 + c] = result.pixels[i * 4 + c];
            const url = URL.createObjectURL(new Blob([ppm], { type: "image/x-portable-pixmap" }));
            const link = document.createElement("a");
            link.href = url;
            link.download = `frame_${Math.round(this.time * this.fps)}.ppm`;
            link.click();
            URL.revokeObjectURL(url);
        }));
        nle.append(this.assetPicker(media), this.button("Preview sequence", () => bridge.edit("sequence")), this.button("Add track", () => bridge.edit("track.add", 0, 0, 0, 0, "Video")), this.button("Add clip", () => bridge.edit("clip.add", +this.track.value, source.selectedIndex, +start.value, +length.value, media.value)), this.button("Trim clip", () => bridge.edit("clip.trim", +this.track.value, +this.clip.value, +start.value, +length.value)), this.button("Move clip", () => bridge.edit("clip.move", +this.track.value, +this.clip.value, +destinationTrack.value, +start.value)), this.button("Split at playhead", () => bridge.edit("clip.split", +this.track.value, +this.clip.value, 0, this.currentFrame())), this.button("Duplicate clip", () => bridge.edit("clip.duplicate", +this.track.value, +this.clip.value, +destinationTrack.value, +start.value)), this.button("Slip source", () => bridge.edit("clip.slip", +this.track.value, +this.clip.value, 0, +slip.value)), this.button("Rename clip", () => bridge.edit("clip.name", +this.track.value, +this.clip.value, 0, 0, name.value)), this.button("Enable clip", () => bridge.edit("clip.enabled", +this.track.value, +this.clip.value, 0, 1)), this.button("Disable clip", () => bridge.edit("clip.enabled", +this.track.value, +this.clip.value, 0, 0)), this.button("Delete clip", () => bridge.edit("clip.remove", +this.track.value, +this.clip.value)), this.button("Ripple delete", () => bridge.edit("clip.ripple_delete", +this.track.value, +this.clip.value)), this.button("Insert gap", () => bridge.edit("track.insert_gap", +this.track.value, 0, +start.value, +length.value)), this.button("Rename track", () => bridge.edit("track.name", +this.track.value, 0, 0, 0, name.value)), this.button("Mute / unmute track", () => bridge.edit("track.mute", +this.track.value, 0, 0, this.sequence?.tracks[+this.track.value]?.muted ? 0 : 1)), this.button("Solo / unsolo track", () => bridge.edit("track.solo", +this.track.value, 0, 0, this.sequence?.tracks[+this.track.value]?.solo ? 0 : 1)), this.button("Move track", () => bridge.edit("track.move", +this.track.value, 0, 0, +destinationTrack.value)), this.button("Remove track", () => bridge.edit("track.remove", +this.track.value)), this.button("Fit timeline", () => this.nle.fit()), this.button("Toggle snapping", () => { this.nle.snapping = !this.nle.snapping; }), this.timeline);
        this.nle = new NLETimeline(this.timeline, (track, clip) => {
            this.track.value = String(track);
            this.clip.value = String(clip);
            this.refreshNLE();
            this.refreshColors.forEach(refresh => refresh());
        }, frame => { this.playing = false; this.stopAudio(); cancelAnimationFrame(this.frameRequest); this.time = frame / this.fps; this.render(); }, (op, a, b, c, value) => this.perform(() => { bridge.edit(op, a, b, c, value); if (op === "clip.move") {
            this.track.value = String(c);
            this.clip.value = String(c === a ? b : bridge.sequenceState().tracks[c].clips.length - 1);
        } }));
        this.refreshNLE = () => {
            this.sequence = bridge.sequenceState();
            this.fps = this.sequence.fpsNum / this.sequence.fpsDen;
            this.duration = this.sequence.duration;
            const selected = this.sequence.tracks[+this.track.value]?.clips[+this.clip.value];
            if (selected) {
                start.value = String(selected.start);
                length.value = String(selected.length);
                name.value = selected.name;
            }
            this.nle.update(this.sequence, +this.track.value, +this.clip.value, this.currentFrame());
            const audio = selected?.audio;
            clipGain.value = String(audio?.gain ?? 1);
            pan.value = String(audio?.pan ?? 0);
            fadeIn.value = String(audio?.fadeIn ?? 0);
            fadeOut.value = String(audio?.fadeOut ?? 0);
            trackGain.value = String(this.sequence.tracks[+this.track.value]?.audioGain ?? 1);
        };
        const audioPanel = this.panel("Audio mixing"), clipGain = this.number(1, 0, 16), trackGain = this.number(1, 0, 16);
        const pan = this.number(0, -1, 1), fadeIn = this.number(0, 0), fadeOut = this.number(0, 0);
        for (const [title, input, op] of [["Clip gain", clipGain, "clip.audio.gain"], ["Stereo pan", pan, "clip.audio.pan"],
            ["Fade in (frames)", fadeIn, "clip.audio.fade_in"], ["Fade out (frames)", fadeOut, "clip.audio.fade_out"],
            ["Track audio gain", trackGain, "track.audio.gain"]]) {
            this.field(audioPanel, title, input);
            input.onchange = () => this.perform(() => bridge.edit(op, +this.track.value, +this.clip.value, 0, +input.value));
        }
        audioPanel.append(this.button("Enable clip audio", () => bridge.edit("clip.audio.enabled", +this.track.value, +this.clip.value, 0, 1)), this.button("Mute clip audio", () => bridge.edit("clip.audio.enabled", +this.track.value, +this.clip.value, 0, 0)));
        const exportPanel = this.panel("Encoded video export"), exportName = this.text("sequence.mp4"), exportStart = this.number(0, 0), exportFrames = this.number(0, 0), codec = this.text("");
        this.field(exportPanel, "Video output (.mp4/.mov/.mkv/.webm)", exportName);
        this.field(exportPanel, "Export start frame", exportStart);
        this.field(exportPanel, "Export frame count (0: full sequence)", exportFrames);
        this.field(exportPanel, "Video encoder (empty: default)", codec);
        exportPanel.append(this.button("Export video with audio", () => this.startExport(exportName.value, +exportStart.value, +exportFrames.value, true, codec.value)), this.button("Export silent video", () => this.startExport(exportName.value, +exportStart.value, +exportFrames.value, false, codec.value)), this.button("Cancel video export", () => { this.exportJob?.cancel(); this.exportJob?.dispose(); this.exportJob = undefined; cancelAnimationFrame(this.exportRequest); }));
        const layers = this.panel("Layer Effects");
        const kind = this.select(["invert", "opacity", "posterize", "transform", "luma_key"]);
        const param = this.text("amount"), amount = this.number(0), destination = this.number(0, 0);
        this.field(layers, "Effect", this.effect);
        this.field(layers, "Operator", kind);
        this.field(layers, "Parameter name", param);
        this.field(layers, "Value", amount);
        this.field(layers, "Move to", destination);
        const stack = (op, value = 0, text = "") => bridge.edit(op, +this.track.value, +this.clip.value, +this.effect.value, value, text);
        layers.append(this.button("Add effect", () => stack("effect.add", 0, kind.value)), this.button("Set parameter", () => stack("effect.param", +amount.value, param.value)), this.button("Enable", () => stack("effect.enabled", 1)), this.button("Bypass", () => stack("effect.enabled", 0)), this.button("Move", () => stack("effect.move", +destination.value)), this.button("Remove", () => stack("effect.remove")));
        const opacity = this.number(1, 0, 1);
        this.field(layers, "Effect opacity", opacity);
        layers.append(this.button("Set opacity", () => stack("effect.opacity", +opacity.value)));
        const colors = bridge.colorOperators();
        this.colorPanel("Color Calibration", "calibration", colors);
        this.colorPanel("Color Grading", "grade", colors);
        const nodes = this.panel("Node Compositing");
        const catalog = bridge.nodeKinds(), nodeKind = this.select([]), search = this.text("");
        const populate = () => {
            nodeKind.replaceChildren();
            const find = search.value.toLowerCase();
            for (const k of catalog)
                if (`${k.category} ${k.label} ${k.name}`.toLowerCase().includes(find)) {
                    const option = document.createElement("option");
                    option.value = k.name;
                    option.textContent = `${k.category} / ${k.label}`;
                    nodeKind.append(option);
                }
        };
        search.oninput = populate;
        populate();
        this.field(nodes, "Find operator", search);
        this.field(nodes, "Node kind", nodeKind);
        this.field(nodes, "Selected node (0-based)", this.node);
        const graphWidth = this.number(320, 1, 4096), graphHeight = this.number(180, 1, 4096), seconds = this.number(0, 0);
        this.field(nodes, "Composition width", graphWidth);
        this.field(nodes, "Composition height", graphHeight);
        this.field(nodes, "Preview seconds", seconds);
        seconds.onchange = () => { this.time = +seconds.value; this.render(); };
        this.node.step = "1";
        nodes.append(this.button("Preview graph", () => { bridge.edit("graph"); this.previewNode = null; }), this.button("New composition", () => { bridge.edit("graph.new", +graphWidth.value, +graphHeight.value); this.node.value = "0"; this.previewNode = null; this.time = 0; }), this.button("Set composition size", () => bridge.edit("graph.size", +graphWidth.value, +graphHeight.value)), this.button("Add node", () => { bridge.edit("node.add", 0, 0, 0, 0, nodeKind.value); this.node.value = String(bridge.graphState().nodes.length - 1); }), this.button("Set output", () => { bridge.edit("node.output", +this.node.value); bridge.edit("graph"); this.previewNode = null; }), this.button("Preview selected node", () => { bridge.edit("graph"); this.previewNode = +this.node.value; }), this.button("Duplicate node", () => this.duplicateNode()), this.button("Reset node", () => bridge.edit("node.reset", +this.node.value)), this.button("Delete node", () => { bridge.edit("node.remove", +this.node.value); this.previewNode = null; }), this.button("Fit nodes", () => this.composition.fit()), this.button("Export composition as PPM", () => {
            const state = bridge.graphState();
            this.downloadPPM(bridge.renderGraphNode(null, this.time, state.width, state.height), "composition.ppm");
        }), this.graph);
        this.composition = new CompositionCanvas(this.graph, catalog, node => { this.node.value = String(node); if (this.previewNode !== null)
            this.previewNode = node; this.refreshGraph(); this.render(); }, (op, a, b, c, value, text) => this.perform(() => bridge.edit(op, a, b, c, value, text)));
        const inspector = document.createElement("div");
        nodes.append(inspector);
        this.refreshGraph = () => {
            const state = bridge.graphState();
            this.graphState = state;
            graphWidth.value = String(state.width);
            graphHeight.value = String(state.height);
            if (!Number.isInteger(+this.node.value) || +this.node.value < 0 || +this.node.value >= state.nodes.length)
                this.node.value = "0";
            if (this.previewNode !== null && this.previewNode >= state.nodes.length)
                this.previewNode = null;
            this.composition.update(state, +this.node.value);
            inspector.replaceChildren();
            const n = +this.node.value, node = state.nodes[n], kind = catalog.find(k => k.name === node?.kind);
            if (!kind || !node) {
                inspector.textContent = "Add a source to begin composing.";
                return;
            }
            const label = this.text(node.label);
            this.field(inspector, "Node label", label);
            label.onchange = () => this.perform(() => bridge.edit("node.label", n, 0, 0, 0, label.value));
            kind.inputs.forEach((port, p) => {
                const connection = this.select(["Disconnected"]);
                connection.options[0].value = "";
                state.nodes.forEach((source, s) => {
                    if (s === n)
                        return;
                    catalog.find(k => k.name === source.kind)?.outputs.forEach((output, o) => {
                        if (output.type !== port.type)
                            return;
                        const option = document.createElement("option");
                        option.value = `${s}:${o}`;
                        option.textContent = `${s}: ${source.label} / ${output.label}`;
                        connection.append(option);
                    });
                });
                const edge = node.inputs[p];
                connection.value = edge ? `${edge.source}:${edge.port}` : "";
                this.field(inspector, `${port.label} (${port.type}${port.required ? ", required" : ""})`, connection);
                connection.onchange = () => this.perform(() => {
                    if (!connection.value)
                        bridge.edit("node.disconnect", n, p);
                    else {
                        const [source, output] = connection.value.split(":").map(Number);
                        bridge.edit("node.connect", source, n, p, output);
                    }
                });
            });
            kind.params.forEach((param, p) => {
                const input = this.number(node.values[p], param.min, param.max);
                input.step = param.integer ? "1" : "any";
                this.field(inspector, param.label, input);
                input.onchange = () => this.perform(() => bridge.edit("node.param", n, 0, 0, +input.value, param.name));
            });
            kind.strings.forEach((name, s) => {
                const input = this.text(node.strings[s] ?? "");
                this.field(inspector, name, input);
                inspector.append(this.assetPicker(input), this.button(`Apply ${name}`, () => bridge.edit("node.path", n, s, 0, 0, input.value)), this.button(`Clear ${name}`, () => bridge.edit("node.path", n, s, 0, 0, "")));
            });
        };
        root.addEventListener("change", this.listener);
        if (bridge.scene3dState)
            this.modeling3dPanel();
        this.perform(() => bridge.edit(bridge.scene3dState?.().active ? "3d" : bridge.graphState().active ? "graph" : "sequence"));
    }
    setZoom(zoom) {
        if (!Number.isFinite(zoom) || zoom < 0.25 || zoom > 8)
            throw new RangeError("zoom must be between 0.25 and 8");
        this.zoom = zoom;
        for (const canvas of [this.preview, this.timeline, this.graph]) {
            canvas.style.transformOrigin = "top left";
            canvas.style.transform = `scale(${zoom})`;
        }
    }
    panel(title) {
        const section = document.createElement("section"), heading = document.createElement("h2");
        heading.textContent = title;
        section.append(heading);
        this.root.append(section);
        return section;
    }
    modeling3dPanel() {
        const panel = this.panel("3D Modeling & Animation"), objects = this.select([]), inspector = document.createElement("div"), keys = document.createElement("pre");
        panel.append(this.button("Preview 3D workspace", () => { this.previewNode = null; this.bridge.edit("3d"); }), this.button("New 3D scene", () => { this.bridge.edit("3d.new"); this.time = 0; }));
        const segments = this.number(64, 8, 128);
        this.field(panel, "Primitive segments", segments);
        for (const primitive of ["cube", "sphere", "plane", "cylinder", "cone", "torus", "capsule", "pyramid", "disk", "nurbs", "metaball",
            "tube", "hemisphere", "wedge", "tetrahedron", "octahedron", "icosahedron"])
            panel.append(this.button(`Add ${primitive}`, () => this.bridge.edit("3d.add", +segments.value, 0, 0, 0, primitive)));
        let drag;
        this.preview.style.touchAction = "none";
        this.preview.oncontextmenu = event => { if (this.scene3d?.active)
            event.preventDefault(); };
        this.preview.onpointerdown = event => {
            if (!this.scene3d?.active || drag)
                return;
            this.perform(() => this.bridge.edit("3d.navigation_begin"));
            drag = { x: event.clientX, y: event.clientY, button: event.button, id: event.pointerId };
            this.preview.setPointerCapture?.(event.pointerId);
            event.preventDefault();
        };
        this.preview.onpointermove = event => {
            if (!drag || drag.id !== event.pointerId)
                return;
            const dx = event.clientX - drag.x, dy = event.clientY - drag.y;
            drag.x = event.clientX;
            drag.y = event.clientY;
            const pan = drag.button !== 0 || event.shiftKey;
            const scale = 2 * (this.scene3d?.camera[2] ?? 7) * Math.tan((this.scene3d?.camera[6] ?? 45) * Math.PI / 360) / Math.max(1, this.preview.getBoundingClientRect().height);
            this.perform(() => this.bridge.edit(pan ? "3d.pan" : "3d.orbit", 0, 0, 0, 0, pan ? `${-dx * scale} ${dy * scale} 0` : event.altKey ? `0 0 ${dx * .4}` : `${-dx * .4} ${-dy * .4} 0`));
        };
        this.preview.onpointerup = event => {
            if (!drag || drag.id !== event.pointerId)
                return;
            drag = undefined;
            this.perform(() => this.bridge.edit("3d.navigation_end"));
        };
        this.preview.onpointercancel = () => { if (drag) {
            drag = undefined;
            this.perform(() => this.bridge.edit("3d.navigation_cancel"));
        } };
        this.preview.onlostpointercapture = this.preview.onpointercancel;
        this.cancelSceneNavigation = () => {
            if (drag) {
                drag = undefined;
                this.perform(() => this.bridge.edit("3d.navigation_cancel"));
            }
        };
        this.preview.onwheel = event => {
            if (!this.scene3d?.active)
                return;
            event.preventDefault();
            event.stopPropagation();
            this.perform(() => this.bridge.edit("3d.dolly", 0, 0, 0, Math.max(-10, Math.min(10, -event.deltaY * .002))));
        };
        const navigation = document.createElement("p");
        navigation.textContent = "Viewport: drag to orbit, Shift/right-drag to pan, Alt-drag to roll, wheel to zoom. Quaternion rotation can pass through either pole.";
        panel.append(navigation);
        ["Front", "Right", "Top", "Back", "Left", "Bottom"].forEach((view, i) => panel.append(this.button(`3D ${view}`, () => this.bridge.edit("3d.view", i))));
        for (const [axis, text] of [["X", "1 0 0"], ["Y", "0 1 0"], ["Z", "0 0 1"]])
            panel.append(this.button(`Gimbal ${axis}`, () => this.bridge.edit("3d.orbit_axis", 0, 0, 0, 15, text)));
        const scriptChannel = this.select(["Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y", "Rotation Z", "Scale X", "Scale Y", "Scale Z"]), script = document.createElement("textarea");
        script.value = "(defkernel spin [time frame index value] (+ value (* time 90)))";
        this.field(panel, "Joltscript channel", scriptChannel);
        this.field(panel, "Joltscript animation", script);
        panel.append(this.button("Load assigned animation script", () => { script.value = this.scene3d?.objects[this.sceneObject]?.scripts?.[scriptChannel.selectedIndex] ?? ""; }), this.button("Apply animation script", () => this.bridge.edit("3d.script", this.sceneObject, scriptChannel.selectedIndex, 0, 0, script.value)), this.button("Remove animation script", () => this.bridge.edit("3d.script", this.sceneObject, scriptChannel.selectedIndex, 0, 0, "")));
        const scriptPath = this.text("spin.jolt");
        this.field(panel, "Animation .jolt path", scriptPath);
        panel.append(this.assetPicker(scriptPath), this.button("Load animation .jolt", () => this.bridge.edit("3d.script_file", this.sceneObject, scriptChannel.selectedIndex, 0, 0, scriptPath.value)));
        let generatorPoint = 0;
        this.field(panel, "Scene object", objects);
        panel.append(inspector, keys);
        objects.onchange = () => { this.sceneObject = objects.selectedIndex; this.refreshScene(); };
        const ply = this.text("mesh.ply"), vertex = this.number(0, 0), axis = this.select(["X", "Y", "Z"]), coordinate = this.number(0);
        this.field(panel, "PLY path", ply);
        panel.append(this.assetPicker(ply), this.button("Import PLY", () => this.bridge.edit("3d.import_ply", 0, 0, 0, 0, ply.value)), this.button("Export PLY", () => {
            this.bridge.edit("3d.export_ply", this.sceneObject, 0, 0, 0, ply.value);
            const bytes = this.bridge.readAsset?.(ply.value);
            if (bytes) {
                const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)], { type: "application/octet-stream" }));
                const link = document.createElement("a");
                link.href = url;
                link.download = "mesh.ply";
                link.click();
                URL.revokeObjectURL(url);
            }
        }));
        this.field(panel, "Vertex index", vertex);
        this.field(panel, "Vertex axis", axis);
        this.field(panel, "Vertex coordinate", coordinate);
        panel.append(this.button("Set vertex coordinate", () => this.bridge.edit("3d.vertex", this.sceneObject, +vertex.value, axis.selectedIndex, +coordinate.value)), this.button("Subdivide mesh", () => this.bridge.edit("3d.subdivide", this.sceneObject)), this.button("Align principal axes", () => this.bridge.edit("3d.align", this.sceneObject)), this.button("Duplicate mesh", () => this.bridge.edit("3d.duplicate", this.sceneObject)), this.button("Delete mesh", () => this.bridge.edit("3d.remove", this.sceneObject)), this.button("Export 3D frame as PPM", () => { this.bridge.edit("3d"); this.downloadPPM(this.bridge.renderFrame(this.time), "scene3d.ppm"); }));
        const bake = this.number(60, 1, 600);
        this.field(panel, "Rigid body bake frames", bake);
        panel.append(this.button("Bake rigid-body animation", () => this.bridge.edit("3d.bake", 0, 0, +bake.value)));
        this.refreshScene = () => {
            this.scene3d = this.bridge.scene3dState?.();
            const state = this.scene3d;
            if (!state)
                return;
            if (state.active) {
                this.fps = state.fps;
                this.duration = state.frames;
            }
            this.sceneObject = Math.max(0, Math.min(this.sceneObject, state.objects.length - 1));
            objects.replaceChildren();
            for (const object of state.objects) {
                const option = document.createElement("option");
                option.textContent = object.name;
                objects.append(option);
            }
            objects.selectedIndex = this.sceneObject;
            inspector.replaceChildren();
            const clockFPS = this.number(state.fps, 1, 240), duration = this.number(state.frames, 1, 36000);
            this.field(inspector, "3D FPS", clockFPS);
            this.field(inspector, "3D duration frames", duration);
            inspector.append(this.button("Set 3D clock", () => this.bridge.edit("3d.clock", +clockFPS.value, +duration.value)));
            ["Orbit yaw", "Orbit pitch", "Camera distance", "Target X", "Target Y", "Target Z", "Field of view"].forEach((name, i) => {
                const input = this.number(state.camera[i]);
                this.field(inspector, name, input);
                input.onchange = () => this.perform(() => this.bridge.edit("3d.camera", i, 0, 0, +input.value));
            });
            const object = state.objects[this.sceneObject];
            keys.textContent = "";
            if (!object)
                return;
            const label = this.text(object.name), mass = this.number(object.mass, 0, 10000);
            this.field(inspector, "3D object name", label);
            label.onchange = () => this.perform(() => this.bridge.edit("3d.name", this.sceneObject, 0, 0, 0, label.value));
            this.field(inspector, "Mass (0 = static)", mass);
            mass.onchange = () => this.perform(() => this.bridge.edit("3d.mass", this.sceneObject, 0, 0, +mass.value));
            inspector.append(this.button(object.visible ? "Hide mesh" : "Show mesh", () => this.bridge.edit("3d.visible", this.sceneObject, 0, 0, object.visible ? 0 : 1)));
            inspector.append(this.button(object.smooth ? "Flat shading" : "Smooth shading", () => this.bridge.edit("3d.smooth", this.sceneObject, 0, 0, object.smooth ? 0 : 1)));
            if (object.generator) {
                const resolution = this.number(object.resolution ?? 32, 8, 64), nurbs = object.generator === 1, points = (nurbs ? object.controls : object.balls) ?? [];
                this.field(inspector, "Surface resolution", resolution);
                resolution.onchange = () => this.perform(() => this.bridge.edit("3d.resolution", this.sceneObject, +resolution.value));
                const point = this.select(points.map((_, i) => `${nurbs ? "Control" : "Ball"} ${i}`));
                generatorPoint = Math.max(0, Math.min(generatorPoint, points.length - 1));
                point.selectedIndex = generatorPoint;
                this.field(inspector, nurbs ? "NURBS control point" : "Metaball index", point);
                point.onchange = () => { generatorPoint = point.selectedIndex; this.refreshScene(); };
                ["X", "Y", "Z", nurbs ? "Weight" : "Radius"].forEach((label, ch) => {
                    const value = this.number(points[generatorPoint]?.[ch] ?? (ch === 3 ? 1 : 0));
                    this.field(inspector, `Generator ${label}`, value);
                    value.onchange = () => this.perform(() => this.bridge.edit(nurbs ? "3d.nurbs_point" : "3d.metaball_point", this.sceneObject, generatorPoint, ch, +value.value));
                });
                if (!nurbs)
                    inspector.append(this.button("Add metaball", () => this.bridge.edit("3d.metaball_add", this.sceneObject, 0, 0, 0, "0 1 0 1")), this.button("Remove metaball", () => this.bridge.edit("3d.metaball_remove", this.sceneObject, generatorPoint)));
                inspector.append(this.button("Make generator editable", () => this.bridge.edit("3d.make_editable", this.sceneObject)));
            }
            const cloner = this.select(["Off", "Linear", "Radial", "Grid"]), count = this.number(object.cloner?.count ?? 1, 1, 64), spacing = this.number(object.cloner?.spacing ?? 3, .01, 1000);
            cloner.selectedIndex = object.cloner?.mode ?? 0;
            this.field(inspector, "Cloner arrangement", cloner);
            this.field(inspector, "Clone count", count);
            this.field(inspector, "Clone spacing / radius", spacing);
            inspector.append(this.button("Apply cloner", () => this.bridge.edit("3d.cloner", this.sceneObject, cloner.selectedIndex, +count.value, +spacing.value)), this.button("Make clones real at playhead", () => this.bridge.edit("3d.cloner_make_real", this.sceneObject, 0, 0, this.time)));
            ["Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y", "Rotation Z", "Scale X", "Scale Y", "Scale Z"].forEach((name, ch) => {
                const input = this.number(object.transform[ch]);
                this.field(inspector, name, input);
                input.onchange = () => this.perform(() => this.bridge.edit("3d.transform", this.sceneObject, ch, 0, +input.value));
                inspector.append(this.button(`Key ${name}`, () => this.bridge.edit("3d.key", this.sceneObject, ch, this.currentFrame(), +input.value)), this.button(`Remove ${name} key`, () => this.bridge.edit("3d.key_remove", this.sceneObject, ch, this.currentFrame())));
                const curve = this.select(["Hold", "Linear", "Smooth"]);
                this.field(inspector, `${name} interpolation`, curve);
                curve.onchange = () => this.perform(() => this.bridge.edit("3d.interpolation", this.sceneObject, ch, this.currentFrame(), curve.selectedIndex));
            });
            ["R", "G", "B"].forEach((name, ch) => {
                const input = this.number(object.color[ch], 0, 1);
                this.field(inspector, `Material ${name}`, input);
                input.onchange = () => this.perform(() => this.bridge.edit("3d.color", this.sceneObject, ch, 0, +input.value));
            });
            keys.textContent = `${object.vertices} vertices / ${object.triangles} triangles\n` + object.keys.map(key => `Channel ${key.channel}: frame ${key.frame} = ${key.value} (curve ${key.interpolation})`).join("\n");
        };
    }
    colorPanel(title, section, catalog) {
        const panel = this.panel(title), operators = catalog.filter(op => op.section === section);
        const add = this.select(operators.map(op => op.name)), selected = this.select([]);
        for (let i = 0; i < operators.length; ++i)
            add.options[i].textContent = operators[i].label;
        // Values are stable identifiers even when display labels are translated.
        operators.forEach((op, i) => { add.options[i].value = op.name; });
        add.value = section === "grade" ? "grade_primary" : "calib_white_balance";
        const controls = document.createElement("div");
        const command = (action, value = 0, text = "") => this.bridge.edit(`${section}.${action}`, +this.track.value, +this.clip.value, action === "add" ? 0 : selected.selectedIndex, value, text);
        this.field(panel, "Add operator", add);
        panel.append(this.button("Add", () => command("add", 0, add.value)));
        this.field(panel, "Color layer", selected);
        panel.append(this.button("Enable", () => command("enabled", 1)), this.button("Bypass", () => command("enabled", 0)), this.button("Reset", () => command("reset")), this.button("Remove", () => command("remove")), this.button("Move up", () => command("move", selected.selectedIndex - 1)), this.button("Move down", () => command("move", selected.selectedIndex + 1)), controls);
        const refresh = () => {
            const layers = colorLayers(this.document, +this.track.value, +this.clip.value, operators);
            const index = Math.max(0, Math.min(selected.selectedIndex, layers.length - 1));
            selected.replaceChildren();
            layers.forEach((layer, i) => { const option = document.createElement("option"); option.textContent = `${i + 1}. ${layer.op.label}${layer.enabled ? "" : " (bypassed)"}`; selected.append(option); });
            selected.selectedIndex = layers.length ? index : -1;
            controls.replaceChildren();
            const layer = layers[index];
            if (!layer) {
                controls.textContent = "Add a color operator to the selected timeline clip.";
                return;
            }
            if (layer.op.path) {
                const path = this.text(layer.words[2] ?? "");
                this.field(controls, "LUT file", path);
                controls.append(this.assetPicker(path), this.button("Load LUT", () => command("path", 0, path.value)), this.button("Clear LUT", () => command("path", 0, "")));
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
    number(value, min, max) {
        const input = document.createElement("input");
        input.type = "number";
        input.value = String(value);
        input.step = "any";
        if (min !== undefined)
            input.min = String(min);
        if (max !== undefined)
            input.max = String(max);
        return input;
    }
    text(value) { const input = document.createElement("input"); input.value = value; return input; }
    select(values) {
        const input = document.createElement("select");
        for (const value of values) {
            const option = document.createElement("option");
            option.textContent = value;
            input.append(option);
        }
        return input;
    }
    field(parent, title, input) {
        const label = document.createElement("label");
        label.append(document.createTextNode(title + " "), input);
        parent.append(label);
    }
    button(title, action) {
        const button = document.createElement("button");
        button.textContent = title;
        button.onclick = () => this.perform(action);
        return button;
    }
    assetPicker(path) {
        const file = document.createElement("input");
        file.type = "file";
        file.setAttribute("aria-label", "Import media or LUT");
        file.onchange = () => {
            const asset = file.files?.[0];
            if (asset)
                void asset.arrayBuffer().then(bytes => { this.stopAudio(); path.value = this.bridge.importAsset(asset.name, new Uint8Array(bytes)); }).catch(e => this.fail(e));
        };
        return file;
    }
    perform(action) {
        this.stopAudio();
        try {
            action();
            this.composition.cancel();
            this.document = this.bridge.sequenceDocument();
            this.status.textContent = "";
            this.refreshNLE();
            this.refreshColors.forEach(refresh => refresh());
            this.refreshGraph();
            this.refreshScene();
            this.render();
        }
        catch (error) {
            this.fail(error);
        }
    }
    duplicateNode() {
        this.bridge.edit("node.duplicate", +this.node.value);
        this.node.value = String(this.bridge.graphState().nodes.length - 1);
    }
    fail(error) { this.status.textContent = error instanceof Error ? error.message : String(error); }
    currentFrame() { return Math.floor(this.time * this.fps + 1e-7); }
    render() {
        try {
            const result = this.graphState?.active && this.previewNode !== null ? this.bridge.renderGraphNode(this.previewNode, this.time) : this.bridge.renderFrame(this.time);
            this.preview.width = result.width;
            this.preview.height = result.height;
            this.preview.getContext("2d")?.putImageData(new ImageData(new Uint8ClampedArray(result.pixels), result.width, result.height), 0, 0);
            this.frame.value = String(this.currentFrame());
            if (this.sequence)
                this.nle.update(this.sequence, +this.track.value, +this.clip.value, this.currentFrame());
        }
        catch (error) {
            this.playing = false;
            this.stopAudio();
            this.fail(error);
        }
    }
    downloadPPM(result, name) {
        const header = new TextEncoder().encode(`P6\n${result.width} ${result.height}\n255\n`);
        const ppm = new Uint8Array(header.length + result.width * result.height * 3);
        ppm.set(header);
        for (let i = 0; i < result.width * result.height; i++)
            for (let c = 0; c < 3; c++)
                ppm[header.length + i * 3 + c] = result.pixels[i * 4 + c];
        const url = URL.createObjectURL(new Blob([ppm], { type: "image/x-portable-pixmap" }));
        const link = document.createElement("a");
        link.href = url;
        link.download = name;
        link.click();
        URL.revokeObjectURL(url);
    }
    tick = (timestamp) => {
        if (!this.playing)
            return;
        if (this.previous)
            this.time += (timestamp - this.previous) / 1000;
        const end = this.graphState?.active ? 10 : this.duration / this.fps;
        if (end <= 0) {
            this.time = 0;
            this.playing = false;
        }
        else if (this.time >= end) {
            if (this.loop) {
                this.time %= end;
                this.stopAudio();
            }
            else {
                this.time = this.graphState?.active ? end : Math.max(0, (this.duration - 1) / this.fps);
                this.playing = false;
            }
        }
        this.previous = timestamp;
        this.render();
        if (this.playing)
            this.scheduleAudio();
        else
            this.stopAudio();
        if (this.playing)
            this.frameRequest = requestAnimationFrame(this.tick);
    };
    stopAudio() {
        for (const source of this.audioSources)
            source.stop();
        this.audioSources.clear();
        this.audioWhen = 0;
    }
    scheduleAudio() {
        const context = this.audioContext;
        if (!context || !this.bridge.renderAudio || this.graphState?.active || this.scene3d?.active)
            return;
        try {
            if (!this.audioWhen || this.audioWhen < context.currentTime) {
                this.audioSample = Math.floor(this.time * context.sampleRate);
                this.audioWhen = context.currentTime + 0.03;
            }
            while (this.audioWhen < context.currentTime + 0.2) {
                const count = 4096, pcm = this.bridge.renderAudio(this.audioSample, count, context.sampleRate);
                const buffer = context.createBuffer(2, count, context.sampleRate);
                for (let c = 0; c < 2; ++c) {
                    const samples = buffer.getChannelData(c);
                    for (let i = 0; i < count; ++i)
                        samples[i] = pcm[i * 2 + c];
                }
                const source = context.createBufferSource();
                source.buffer = buffer;
                source.connect(context.destination);
                this.audioSources.add(source);
                source.onended = () => { this.audioSources.delete(source); source.disconnect(); };
                source.start(this.audioWhen);
                this.audioSample += count;
                this.audioWhen += count / context.sampleRate;
            }
        }
        catch (error) {
            this.playing = false;
            this.stopAudio();
            this.fail(error);
        }
    }
    startExport(name, start, count, audio, codec) {
        if (!this.bridge.beginVideoExport)
            throw new Error("Encoded export bridge unavailable");
        if (this.exportJob)
            throw new Error("An export is already running");
        this.exportJob = this.bridge.beginVideoExport(name, start, count || (this.graphState?.active ? 300 : 0), audio, codec);
        const step = () => {
            const job = this.exportJob;
            if (!job)
                return;
            try {
                const result = job.step();
                this.status.textContent = `Exported ${result.completed} frames`;
                if (!result.done) {
                    this.exportRequest = requestAnimationFrame(step);
                    return;
                }
                const url = URL.createObjectURL(new Blob([new Uint8Array(result.bytes)], { type: "application/octet-stream" }));
                const link = document.createElement("a");
                link.href = url;
                link.download = name;
                link.click();
                URL.revokeObjectURL(url);
                job.dispose();
                this.exportJob = undefined;
            }
            catch (error) {
                job.dispose();
                this.exportJob = undefined;
                this.fail(error);
            }
        };
        this.exportRequest = requestAnimationFrame(step);
    }
    download() {
        const url = URL.createObjectURL(new Blob([this.bridge.saveDocument()], { type: "text/plain" }));
        const link = document.createElement("a");
        link.href = url;
        link.download = "project.jfx";
        link.click();
        URL.revokeObjectURL(url);
    }
    dispose() {
        this.cancelSceneNavigation();
        this.preview.onpointerdown = this.preview.onpointermove = this.preview.onpointerup = this.preview.onpointercancel = this.preview.onlostpointercapture = null;
        this.preview.onwheel = null;
        this.preview.oncontextmenu = null;
        this.playing = false;
        this.stopAudio();
        void this.audioContext?.close();
        cancelAnimationFrame(this.frameRequest);
        cancelAnimationFrame(this.exportRequest);
        this.exportJob?.dispose();
        this.exportJob = undefined;
        this.root.removeEventListener("change", this.listener);
        this.root.removeEventListener("keydown", this.shortcuts);
        this.root.replaceChildren();
    }
}
