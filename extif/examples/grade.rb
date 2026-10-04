# Run gain(0.75), or call edit with the included sequence.jfx.
def gain(value)
  JFX.clamp(value * 2, 0, 1)
end

def edit
  JFX.command("grade.add", 0, 0, 0, 0, "grade_primary")
  JFX.command("grade.param", 0, 0, 0, 1, "exposure")
end

def scale_buffer(buffer, factor)
  JFX.size(buffer).times do |i|
    JFX.write(buffer, i, JFX.read(buffer, i) * factor)
  end
  nil
end
JFX.register_kernel("scale_buffer", "scale_buffer")
