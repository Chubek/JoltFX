-- Run gain(0.75), or call edit() with the included sequence.jfx.
function gain(value)
    return jfx.clamp(value * 2, 0, 1)
end

function edit()
    jfx.command("grade.add", 0, 0, 0, 0, "grade_primary")
    jfx.command("grade.param", 0, 0, 0, 1, "exposure")
end

function scale_buffer(buffer, factor)
    for i = 0, jfx.size(buffer) - 1 do
        jfx.write(buffer, i, jfx.read(buffer, i) * factor)
    end
end
jfx.register_kernel("scale_buffer", scale_buffer)
