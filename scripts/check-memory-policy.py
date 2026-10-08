#!/usr/bin/env python3
"""Keep application-owned native storage on the Tilly/MemTKX path."""
import re
import sys
from pathlib import Path

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
extensions = {".c", ".cpp", ".cc", ".cxx", ".h", ".hpp", ".inc", ".m", ".mm"}
bootstrap = {"tilly/src/allocator.cpp", "tilly/include/tilly/memory.hpp"}
tokens = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
allocation = re.compile(r"(?<![\w.>])(?:std::)?(?:malloc|calloc|realloc|free|strdup|strndup|aligned_alloc|posix_memalign)\s*\(|\bnew\s+(?!\s*\()\w|\bdelete\s+(?:\[\]\s*)?\w|std::(?:vector|deque|list|forward_list|basic_string|string|wstring|u8string|u16string|u32string|set|multiset|unordered_set|unordered_multiset|map|multimap|unordered_map|unordered_multimap|make_unique|make_shared)\b")
failures = []
for directory in ("src", "tilly", "backends", "extif", "kernels", "frontends", "joltscript", "examples", "sdk"):
    for path in (root / directory).rglob("*"):
        relative = path.relative_to(root).as_posix()
        if path.suffix not in extensions or relative in bootstrap or any(p in {"tests", "build", ".cxx", "node_modules"} for p in path.parts):
            continue
        source = tokens.sub(lambda m: "\n" * m[0].count("\n"), path.read_text())
        for match in allocation.finditer(source):
            failures.append(f"{relative}:{source.count(chr(10), 0, match.start()) + 1}: {match[0]}")
if failures:
    sys.exit("Native storage bypasses Tilly/MemTKX:\n" + "\n".join(failures))
print("Native allocation policy passed")
