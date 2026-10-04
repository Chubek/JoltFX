"""Real SDK module loading, terminal actions/history and project rendering."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile

cli, example = sys.argv[1:3]


def run(*args, input=None, success=True):
    result = subprocess.run([cli, *map(str, args)], input=input, text=True, capture_output=True)
    assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
    return result


with tempfile.TemporaryDirectory(prefix="joltfx-sdk-", dir=os.environ.get("TMPDIR")) as directory:
    root = Path(directory)
    module = root / ("module with spaces" + Path(example).suffix)
    shutil.copyfile(example, module)
    info = run("plugins", "inspect", module).stdout
    assert "org.joltfx.example" in info and "org.joltfx.example.apply" in info
    run("plugins", "inspect", root / "missing.so", success=False)
    source, edited, raster = [root / name for name in ("source.jfx", "edited.jfx", "frame.ppm")]
    source.write_text("size 4 2\nfps 30 1\ntrack Video\nclip solid 0 30 0.6 0.3 0.15 1 0 0 0 0\n")
    commands = "plugins\nplugin.action org.joltfx.example.apply 0 0 0\neffect.param 0 0 0 0.5 amount\nundo\nsave\n"
    result = run("edit", source, edited, "--plugin", module, input=commands)
    assert "Warm Tint" in result.stdout and "org.joltfx.example.tint" in edited.read_text()
    run("plugins", "render", module, edited, raster)
    header, width, maximum, pixels = raster.read_bytes().split(b"\n", 3)
    assert (header, width, maximum) == (b"P6", b"4 2", b"255")
    assert len(pixels) == 24 and pixels[0] in (178, 179) and pixels[1:3] == bytes([72, 33])
    # Failed load/render must not replace an existing project/image.
    edited.write_text("keep")
    run("edit", source, edited, "--plugin", root / "missing.so", input="save\n", success=False)
    assert edited.read_text() == "keep"
    source.write_text("graph\nsize 4 2\nnode unknown.plugin.effect Bad\noutput 1\n")
    raster.write_bytes(b"keep")
    run("plugins", "render", module, source, raster, success=False)
    assert raster.read_bytes() == b"keep"
    # Image-profile kernel marshaling preserves straight RGB with fractional alpha.
    source.write_text("graph\nsize 4 2\nnode color Source\nparam 1 r 0.2\nparam 1 g 0.4\n"
                      "param 1 b 0.6\nparam 1 a 0.5\nnode solid Frame\nlink 1 0 -> 2 0\n"
                      "node org.joltfx.example.invert Invert\nlink 2 0 -> 3 0\noutput 3\n")
    run("plugins", "render", module, source, raster, 10)
    pixels = raster.read_bytes().split(b"\n", 3)[3]
    assert pixels[:3] == bytes([204, 153, 102]) and pixels == pixels[:3] * 8
    # Unused modules can be unloaded through the terminal interface.
    source.write_text("size 4 2\nfps 30 1\ntrack Video\nclip solid 0 30 0.6 0.3 0.15 1 0 0 0 0\n")
    run("edit", source, edited, input=f"plugin.load {module}\nplugin.unload 1\nsave\n")
    # Encoded export loads the plugin before parsing its project snapshot.
    if len(sys.argv) > 3 and "encoded_video: yes" in run("capabilities").stdout:
        import json
        output = root / "plugin.mkv"
        run("edit", source, edited, "--plugin", module,
            input="plugin.action org.joltfx.example.apply 0 0 0\nsave\n")
        run("export-video", edited, "-o", output, "--plugin", module, "--frames", 2, "--no-audio")
        probe = subprocess.run([sys.argv[3], "-v", "error", "-count_frames", "-show_streams", "-of", "json", str(output)],
                               capture_output=True, text=True, check=True)
        streams = json.loads(probe.stdout)["streams"]
        assert len(streams) == 1 and streams[0]["codec_name"] == "ffv1" and streams[0]["nb_read_frames"] == "2"

print("SDK CLI load/actions/history/render/transactional-output checks passed")
