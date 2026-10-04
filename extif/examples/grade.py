import jfx


def gain(value):
    return jfx.clamp(value * 2, 0, 1)


def edit():
    jfx.command("grade.add", 0, 0, 0, 0, "grade_primary")
    jfx.command("grade.param", 0, 0, 0, 1, "exposure")


def scale_buffer(buffer, factor):
    for i in range(jfx.size(buffer)):
        jfx.write(buffer, i, jfx.read(buffer, i) * factor)


jfx.register_kernel("scale_buffer", scale_buffer)
