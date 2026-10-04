// Run gain(0.75), or call edit() with the included sequence.jfx.
function gain(value) {
    return jfx.clamp(value * 2, 0, 1);
}

function edit() {
    jfx.command("grade.add", 0, 0, 0, 0, "grade_primary");
    jfx.command("grade.param", 0, 0, 0, 1, "exposure");
}

function scale_buffer(buffer, factor) {
    // Engine INT values are BigInt; indices also accept integral Number values.
    for (let i = 0n; i < jfx.size(buffer); i++) {
        jfx.write(buffer, i, jfx.read(buffer, i) * factor);
    }
}
jfx.register_kernel("scale_buffer", scale_buffer);
