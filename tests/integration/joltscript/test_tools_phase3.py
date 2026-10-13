"""Phase 3 Joltscript tool behavior: formatter, docs and LSP semantics."""
import json
import pathlib
import subprocess
import sys

formatter, documentation, lsp, root = sys.argv[1:]
root = pathlib.Path(root)


def run(program, arguments, data=b"", expect=0):
    result = subprocess.run([program, *arguments], input=data, capture_output=True, timeout=10)
    assert result.returncode == expect, (program, arguments, result.returncode, result.stdout, result.stderr)
    return result.stdout, result.stderr


source = b";; @kernel sample\n(defkernel sample [x] (+ x 1))\n"
formatted, _ = run(formatter, ["--stdin"], source)
assert b";; @kernel sample" in formatted
assert b"(defkernel sample" in formatted
assert b"( " not in formatted

math_doc, _ = run(documentation, [str(root / "joltscript/stdlib/math.jolt")])
assert b"jolt.math" in math_doc
assert b"`sin` *(intrinsic)*" in math_doc

_, doc_error = run(documentation, ["--stdin"], b"(defkernel broken [x] (missing x))", expect=1)
assert b"unknown operation" in doc_error or b"undefined binding" in doc_error or b"invalid" in doc_error


def packet(message):
    body = json.dumps(message, separators=(",", ":")).encode()
    return b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body


doc = "(import jolt.math (sin))\n(defkernel bad [] (missing 1))"
messages = b"".join([
    packet({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {}}),
    packet({"jsonrpc": "2.0", "method": "textDocument/didOpen", "params": {"textDocument": {"uri": "file:///bad.jolt", "text": doc}}}),
    packet({"jsonrpc": "2.0", "id": 2, "method": "textDocument/completion", "params": {}}),
    packet({"jsonrpc": "2.0", "method": "exit", "params": {}}),
])
lsp_out, _ = run(lsp, [], messages)
assert b"capabilities" in lsp_out
assert b"publishDiagnostics" in lsp_out
assert b"undefined binding" in lsp_out
assert b"jolt.math" in lsp_out
assert b"defkernel" in lsp_out

print("Joltscript Phase 3 tools passed")
