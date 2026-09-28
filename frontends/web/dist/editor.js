/** All edits and rendering are native. DOM state contains only selection and the clock. */
export class JoltEditor {
    root;
    bridge;
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
    document = "";
    listener = (event) => {
        const target = event.target;
        if (target === this.frame) {
            this.time = Number(this.frame.value) / this.fps;
            this.render();
        }
    };
    constructor(root, bridge) {
        this.root = root;
        this.bridge = bridge;
        root.classList.add("jolt-editor");
        const toolbar = document.createElement("nav");
        root.append(toolbar, this.preview, this.status);
        toolbar.append(this.button("Play / pause", () => {
            this.playing = !this.playing;
            this.previous = 0;
            if (this.playing)
                this.frameRequest = requestAnimationFrame(this.tick);
            else
                cancelAnimationFrame(this.frameRequest);
        }), this.button("New sequence", () => {
            bridge.loadDocument("size 320 180\nfps 30 1\ntrack V1\nclip solid 0 300 0.6 0.3 0.15 1 0 0 0 0 First\n");
        }), this.button("Save project", () => this.download()));
        const open = document.createElement("input");
        open.type = "file";
        open.accept = ".jfx";
        open.setAttribute("aria-label", "Open project");
        open.onchange = () => { const f = open.files?.[0]; if (f)
            void f.text().then(text => this.perform(() => bridge.loadDocument(text))).catch(e => this.fail(e)); };
        toolbar.append(open);
        const nle = this.panel("NLE timeline");
        const start = this.number(0, 0), length = this.number(90, 1), media = this.text("");
        const source = this.select(["solid", "gradient", "checker", "sweep", "image", "video"]);
        this.field(nle, "Frame", this.frame);
        this.field(nle, "Track (0-based)", this.track);
        this.field(nle, "Clip (0-based)", this.clip);
        this.field(nle, "Start", start);
        this.field(nle, "Length", length);
        this.field(nle, "Source", source);
        this.field(nle, "Media path", media);
        nle.append(this.assetPicker(media), this.button("Preview sequence", () => bridge.edit("sequence")), this.button("Add track", () => bridge.edit("track.add", 0, 0, 0, 0, "Video")), this.button("Add clip", () => bridge.edit("clip.add", +this.track.value, source.selectedIndex, +start.value, +length.value, media.value)), this.button("Trim clip", () => bridge.edit("clip.trim", +this.track.value, +this.clip.value, +start.value, +length.value)), this.button("Delete clip", () => bridge.edit("clip.remove", +this.track.value, +this.clip.value)), this.timeline);
        this.timeline.width = 900;
        this.timeline.height = 160;
        this.timeline.onclick = (event) => {
            const box = this.timeline.getBoundingClientRect();
            this.time = Math.max(0, Math.min(this.duration - 1, (event.clientX - box.left) / box.width * this.duration)) / this.fps;
            this.frame.value = String(Math.floor(this.time * this.fps));
            this.render();
        };
        const layers = this.panel("Layer Effects");
        const kind = this.select(["exposure", "contrast", "saturation", "lift_gamma_gain", "lut", "invert", "blur", "opacity"]);
        const param = this.text("stops"), amount = this.number(0), destination = this.number(0, 0);
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
        const grade = this.panel("Color Grading");
        const lut = this.text("");
        this.field(grade, "LUT path", lut);
        grade.append(this.assetPicker(lut));
        const mix = this.number(1, 0, 1);
        this.field(grade, "Mix", mix);
        grade.append(this.button("Add LUT layer", () => stack("effect.add", 0, "lut")), this.button("Load LUT into selected effect", () => stack("effect.path", 0, lut.value)), this.button("Set LUT mix", () => stack("effect.param", +mix.value, "mix")), this.button("Add Lift / Gamma / Gain layer", () => stack("effect.add", 0, "lift_gamma_gain")));
        for (const control of ["lift", "gamma", "gain"])
            for (const channel of ["r", "g", "b"]) {
                const value = this.number(control === "lift" ? 0 : 1, control === "lift" ? -1 : control === "gamma" ? 0.1 : 0, control === "lift" ? 1 : 4);
                this.field(grade, `${control} ${channel}`, value);
                value.onchange = () => this.perform(() => stack("effect.param", +value.value, `${control}_${channel}`));
            }
        const nodes = this.panel("Node Compositing");
        const nodeKind = this.text("exposure"), from = this.number(0, 0), port = this.number(0, 0), nodeParam = this.text("stops"), nodeValue = this.number(0);
        this.field(nodes, "Selected node", this.node);
        this.field(nodes, "Node kind", nodeKind);
        this.field(nodes, "Connect from node", from);
        this.field(nodes, "Input port", port);
        this.field(nodes, "Parameter", nodeParam);
        this.field(nodes, "Value", nodeValue);
        nodes.append(this.button("Preview graph", () => bridge.edit("graph")), this.button("Add node", () => bridge.edit("node.add", 0, 0, 0, 0, nodeKind.value)), this.button("Connect", () => bridge.edit("node.connect", +from.value, +this.node.value, +port.value)), this.button("Disconnect", () => bridge.edit("node.disconnect", +this.node.value, +port.value)), this.button("Set output", () => bridge.edit("node.output", +this.node.value)), this.button("Set parameter", () => bridge.edit("node.param", +this.node.value, 0, 0, +nodeValue.value, nodeParam.value)), this.button("Delete node", () => bridge.edit("node.remove", +this.node.value)), this.graph);
        this.graph.width = 900;
        this.graph.height = 240;
        this.graph.onclick = event => {
            const box = this.graph.getBoundingClientRect();
            const x = (event.clientX - box.left) * 900 / box.width, y = (event.clientY - box.top) * 240 / box.height;
            this.node.value = String(Math.max(0, Math.floor(x / 180) + Math.floor(y / 65) * 5));
        };
        root.addEventListener("change", this.listener);
        this.perform(() => bridge.edit("sequence"));
    }
    panel(title) {
        const section = document.createElement("section"), heading = document.createElement("h2");
        heading.textContent = title;
        section.append(heading);
        this.root.append(section);
        return section;
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
                void asset.arrayBuffer().then(bytes => { path.value = this.bridge.importAsset(asset.name, new Uint8Array(bytes)); }).catch(e => this.fail(e));
        };
        return file;
    }
    perform(action) {
        try {
            action();
            this.document = this.bridge.saveDocument();
            this.status.textContent = "";
            this.drawDocuments();
            this.render();
        }
        catch (error) {
            this.fail(error);
        }
    }
    fail(error) { this.status.textContent = error instanceof Error ? error.message : String(error); }
    render() {
        try {
            const result = this.bridge.renderFrame(this.time);
            this.preview.width = result.width;
            this.preview.height = result.height;
            this.preview.getContext("2d")?.putImageData(new ImageData(new Uint8ClampedArray(result.pixels), result.width, result.height), 0, 0);
            this.frame.value = String(Math.floor(this.time * this.fps));
        }
        catch (error) {
            this.playing = false;
            this.fail(error);
        }
    }
    drawDocuments() {
        const ctx = this.timeline.getContext("2d"), graph = this.graph.getContext("2d");
        if (!ctx || !graph)
            return;
        ctx.clearRect(0, 0, 900, 160);
        graph.clearRect(0, 0, 900, 240);
        const lines = this.document.split("\n");
        let track = -1, node = 0;
        const clips = [];
        for (const line of lines) {
            const words = line.trim().split(/\s+/);
            if (words[0] === "fps")
                this.fps = +words[1] / +words[2];
            if (words[0] === "track")
                track++;
            if (words[0] === "clip") {
                const offset = ["image", "video"].includes(words[1]) ? 3 : 2;
                clips.push({ track, start: +words[offset], length: +words[offset + 1], name: words[1] });
            }
            if (words[0] === "node") {
                const x = node % 5 * 180, y = Math.floor(node / 5) * 65;
                graph.fillStyle = "#285d78";
                graph.fillRect(x + 2, y + 2, 160, 45);
                graph.fillStyle = "white";
                graph.fillText(`${node}: ${words.slice(1).join(" ")}`, x + 8, y + 27);
                node++;
            }
            if (words[0] === "link") {
                const from = +words[1] - 1, to = +words[4] - 1;
                graph.strokeStyle = "#69b9e8";
                graph.beginPath();
                graph.moveTo(from % 5 * 180 + 160, Math.floor(from / 5) * 65 + 22);
                graph.lineTo(to % 5 * 180, Math.floor(to / 5) * 65 + 22);
                graph.stroke();
            }
        }
        if (clips.length)
            this.duration = Math.max(...clips.map(c => c.start + c.length));
        for (const clip of clips) {
            ctx.fillStyle = "#285d78";
            ctx.fillRect(clip.start / this.duration * 900, clip.track * 35 + 2, clip.length / this.duration * 900, 30);
            ctx.fillStyle = "white";
            ctx.fillText(clip.name, clip.start / this.duration * 900 + 4, clip.track * 35 + 22);
        }
    }
    tick = (timestamp) => {
        if (!this.playing)
            return;
        if (this.previous)
            this.time = (this.time + (timestamp - this.previous) / 1000) % (this.duration / this.fps);
        this.previous = timestamp;
        this.render();
        if (this.playing)
            this.frameRequest = requestAnimationFrame(this.tick);
    };
    download() {
        const url = URL.createObjectURL(new Blob([this.bridge.saveDocument()], { type: "text/plain" }));
        const link = document.createElement("a");
        link.href = url;
        link.download = "project.jfx";
        link.click();
        URL.revokeObjectURL(url);
    }
    dispose() { this.playing = false; cancelAnimationFrame(this.frameRequest); this.root.removeEventListener("change", this.listener); this.root.replaceChildren(); }
}
