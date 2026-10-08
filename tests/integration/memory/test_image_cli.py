"""Decode, process and release image storage through the production CLI."""
import subprocess
import sys
import tempfile
from pathlib import Path

with tempfile.TemporaryDirectory() as directory:
    root = Path(directory)
    image = b"P6\n1 2\n255\n" + bytes([1, 2, 3, 4, 5, 6])
    (root / "input.ppm").write_bytes(image)
    (root / "identity.cube").write_text("LUT_3D_SIZE 2\n" + "\n".join(
        f"{r} {g} {b}" for b in range(2) for g in range(2) for r in range(2)) + "\n")
    result = subprocess.run([sys.argv[1], "lut", "apply", str(root / "identity.cube"),
        str(root / "input.ppm"), str(root / "output.ppm")], capture_output=True, timeout=10)
    assert result.returncode == 0, result.stderr.decode(errors="replace")
    assert (root / "output.ppm").read_bytes() == image
print("CLI image decode/process/release passed")
