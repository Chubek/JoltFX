"""Embed an offline-compiled Filament material; no runtime compiler is needed."""
import pathlib
import sys

data = pathlib.Path(sys.argv[1]).read_bytes()
pathlib.Path(sys.argv[2]).write_text(
    "#pragma once\nstatic const unsigned char surface_material[] = {\n"
    + ",".join(str(byte) for byte in data)
    + "\n};\n", encoding="utf-8")
