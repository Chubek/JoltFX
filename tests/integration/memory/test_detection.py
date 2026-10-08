"""Verify real process diagnostics, not just sanitizer shadow queries."""
import os
import subprocess
import sys

executable = sys.argv[1]
env = dict(os.environ, TILLY_CHECK_LEAKS="1")
for mode in ("overflow", "underflow", "freed", "reset", "leak"):
    result = subprocess.run([executable, mode], capture_output=True, env=env, timeout=10)
    assert result.returncode != 0, (mode, "invalid access/leak went undetected")
    expected = b"live allocations" if mode == "leak" else b"ERROR: AddressSanitizer:"
    assert expected in result.stderr, (mode, result.stderr.decode(errors="replace"))
print("MemTKX overrun, underrun, free/reset and leak diagnostics passed")
