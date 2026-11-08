"""Exercise CLI workflow publication and the canonical OTIO/script contracts."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

cli = sys.argv[1]


def run(*args, ok=True, input=None):
    result = subprocess.run([cli, "nle", *map(str, args)], input=input,
                            text=True, capture_output=True)
    assert (result.returncode == 0) == ok, (args, result.stdout, result.stderr)
    return result.stdout


with tempfile.TemporaryDirectory(prefix="jfx-nle-") as directory:
    root = Path(directory)
    project, edited, otio, restored, script = [root / name for name in
        ("source.jfx", "edited.jfx", "edit.otio", "restored.jfx", "batch.jolt")]
    run("new", project, "--size", 64, 64, "--fps", 30000, 1001)
    script.write_text('(defkernel edit [] [["clip.add" 0 5 20 30 "camera.mov"] '
                      '["clip.name" 0 0 0 0 "CLI batch"]])')
    run("script", project, script, edited)
    run("otio-export", edited, otio)
    data = json.loads(otio.read_text())
    children = data["tracks"]["children"][0]["children"]
    assert children[0]["OTIO_SCHEMA"] == "Gap.1"
    assert children[1]["name"] == "CLI batch"
    run("otio-import", otio, restored, 64, 64, 30000, 1001)
    assert json.loads(run("info", restored)) == json.loads(run("info", edited))
    baseline = edited.read_bytes()
    script.write_text('(defkernel edit [] [["clip.name" 0 0 0 0 "Lost"] '
                      '["clip.split" 0 0 0 9999 ""]])')
    run("script", restored, script, edited, ok=False)
    assert edited.read_bytes() == baseline
    run("probe", root / "missing.mov", ok=False)
    run("analyze", root / "missing.wav", "NaN", ok=False)
    run("convert", root / "missing.mov", edited, ok=False)
    assert edited.read_bytes() == baseline
    assert "FFmpeg" in run("encoders")
