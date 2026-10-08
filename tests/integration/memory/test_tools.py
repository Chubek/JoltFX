"""Exercise tool input/allocation edges, including formerly undersized output."""
import subprocess
import sys

formatter, documentation, lsp = sys.argv[1:]

def run(program, arguments, data):
    result = subprocess.run([program, *arguments], input=data, capture_output=True, timeout=10)
    assert result.returncode == 0, (program, result.returncode, result.stderr.decode(errors="replace"))
    return result.stdout

assert run(formatter, ["--stdin"], b"") == b"\n"
assert run(formatter, ["--stdin"], b'"trailing\\').strip() == b'"trailing\\'
assert run(formatter, ["--stdin"], b"(a " + b"b " * 5000 + b")")
assert b"</html>" in run(documentation, ["--stdin", "--html"], b"")
source = b"(defn a [] 1)\n" * 6000
assert run(documentation, ["--stdin", "--html"], source).count(b"<h2>") == 6000
assert run(documentation, ["--stdin"], source).count(b"## `a`") == 6000
for header in (b"-1", b"18446744073709551616", b"16777217", b"3garbage"):
    assert not run(lsp, [], b"Content-Length: " + header + b"\r\n\r\n")
for body in (b'{"method":"initialize","id":1}', b'{"key\\', b"{?}", b'{"method":"exit"}'):
    output = run(lsp, [], b"Content-Length: " + str(len(body)).encode() + b"\r\n\r\n" + body)
    if b"initialize" in body:
        assert b"capabilities" in output
assert not run(lsp, [], b"Content-Length: 100\r\n\r\n{}")
print("Tool memory edge cases passed")
