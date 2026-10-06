"""Execute every enabled language through the installed-style CLI contract."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile

cli, repository = sys.argv[1:3]
examples = Path(repository) / "extif" / "examples"


def run(*args, success=True):
    result = subprocess.run([cli, *map(str, args)], capture_output=True, text=True, timeout=30)
    assert (result.returncode == 0) == success, (args, result.stdout, result.stderr)
    return result


languages = dict(line.split() for line in run("scripts", "list").stdout.splitlines())
assert set(languages) == {"lua", "mruby", "quickjs", "python", "wasm"}
assert "scripts run" in run("help", "scripts").stdout
assert "scripts edit" in run("scripts", "--help").stdout
run("scripts", success=False)
run("scripts", "run", "unknown", "missing", success=False)
suffixes = {"lua": ".lua", "mruby": ".rb", "quickjs": ".js", "python": ".py", "wasm": ".wasm"}
unsafe = {"lua": "io.open('/etc/passwd')", "mruby": "File.open('/etc/passwd')",
          "quickjs": "std.open('/etc/passwd')", "python": "open('/etc/passwd')",
          "wasm": bytes.fromhex("0061736d0100000001040160000002230116776173695f736e617073686f745f70726576696577310866645f77726974650000")}
spin = {"lua": "function spin() while true do end end", "mruby": "def spin; while true; end; end",
        "quickjs": "function spin(){while(true){}}", "python": "def spin():\n while True:\n  pass\n",
        "wasm": bytes.fromhex("0061736d0100000001040160000003020100070801047370696e00000a0901070003400c000b0b")}

with tempfile.TemporaryDirectory(prefix="joltfx-scripts-", dir=os.environ.get("TMPDIR")) as directory:
    root = Path(directory)
    for language, availability in languages.items():
        example = examples / ("grade" + suffixes[language])
        if availability == "disabled":
            assert "not enabled" in run("scripts", "run", language, example, success=False).stderr
            continue
        assert availability == "enabled"
        run("scripts", "run", language, example)
        assert float(run("scripts", "run", language, example, "gain", "0.75").stdout) == 1.0
        run("scripts", "run", language, example, "missing", success=False)
        run("scripts", "run", language, example, "gain", "nan", success=False)
        run("scripts", "run", language, root / "missing", success=False)
        output, raster = root / "edited project.jfx", root / "frame0000.ppm"
        run("scripts", "edit", language, example, examples / "sequence.jfx", output, "edit")
        state = json.loads(run("nle", "info", output).stdout)
        assert state["tracks"][0]["clips"][0]["effects"], state
        run("nle", "render", output, "-o", root / "frame", "--start", "0", "--end", "1")
        pixels = raster.read_bytes().split(b"\n", 3)[3]
        assert pixels[:3] == bytes([64, 128, 191]) and pixels == pixels[:3] * 8, pixels
        script = root / ("blocked" + suffixes[language])
        if language == "wasm":
            script.write_bytes(unsafe[language])
        else:
            script.write_text(unsafe[language])
        output.write_text("keep existing project")
        run("scripts", "edit", language, script, examples / "sequence.jfx", output, success=False)
        assert output.read_text() == "keep existing project"
        if language == "wasm":
            script.write_bytes(spin[language])
        else:
            script.write_text(spin[language])
        failure = run("scripts", "run", language, script, "spin", success=False)
        assert "(-5)" in failure.stderr, (language, failure.stderr)

print("extension CLI examples, grading pixels, budgets, sandbox and output preservation passed")
