"""Real command/scene/PNG round-trip, with output preservation on failed edits."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile

cli = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="jfx-3d-") as directory:
    root = Path(directory)
    def run(*args, commands=None, success=True):
        p = subprocess.run([cli, *map(str, args)], input=commands, text=True, capture_output=True, cwd=root)
        assert (p.returncode == 0) == success, p.stderr
        return p.stdout
    run("3d", "new", "scene.jfx")
    commands = "3d.key 0 0 0 0\n3d.key 0 0 30 2\n3d.subdivide 0 0 0 0\n3d.export_ply 0 0 0 0 mesh.ply\nsave\n"
    run("3d", "edit", "scene.jfx", "animated.jfx", commands=commands)
    state = json.loads(run("3d", "info", "animated.jfx"))
    assert state["active"] and state["objects"][0]["triangles"] == 48
    assert len(state["objects"][0]["keys"]) == 2
    assert "kind: scene3d" in run("project", "info", "animated.jfx")
    run("project", "render", "animated.jfx", "-o", "frame.ppm")
    assert (root / "frame.ppm").read_bytes().startswith(b"P6\n")
    run("3d", "render", "animated.jfx", "a.png", "0")
    run("3d", "render", "animated.jfx", "b.png", "1")
    a, b = (root / "a.png").read_bytes(), (root / "b.png").read_bytes()
    assert a[:8] == b"\x89PNG\r\n\x1a\n" and a != b
    assert (root / "mesh.ply").read_bytes().startswith(b"ply\nformat binary_little_endian")
    (root / "preserve.jfx").write_text("keep me")
    run("3d", "edit", "animated.jfx", "preserve.jfx", commands="3d.transform 0 6 0 0\nsave\n", success=False)
    assert (root / "preserve.jfx").read_text() == "keep me"
