#!/usr/bin/env python3
"""Include-hygiene check: no file may rely on a standard header arriving
transitively through another header.

This exists because of a real failure: the project built cleanly with clang (whose
standard library headers pull in far more than they promise) and failed on MSVC,
which resolves only what a file actually includes. Every symbol listed below must
be reachable through the file's own `#include` lines, or one of the project headers
it includes - never through luck.

  python3 tools/check-includes.py            # exit 1 when something is missing
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

# Symbol -> the standard header that is required to declare it.
REQUIREMENTS = {
    "std::function": "<functional>",
    "std::sort(": "<algorithm>",
    "std::max(": "<algorithm>",
    "std::min(": "<algorithm>",
    "std::find(": "<algorithm>",
    "std::any_of(": "<algorithm>",
    "std::filesystem": "<filesystem>",
    "std::atomic": "<atomic>",
    "std::unique_ptr": "<memory>",
    "std::make_unique": "<memory>",
    "std::shared_ptr": "<memory>",
    "memcpy(": "<cstring>",
    "memset(": "<cstring>",
    "std::strlen": "<cstring>",
    "vsnprintf": "<cstdio>",
    "snprintf(": "<cstdio>",
    "va_list": "<cstdarg>",
    "uint32_t": "<cstdint>",
    "uint64_t": "<cstdint>",
    "int64_t": "<cstdint>",
    "intptr_t": "<cstdint>",
    "size_t": "<cstddef>",
    "std::vector": "<vector>",
    "std::unordered_map": "<unordered_map>",
    "std::map<": "<map>",
    "std::mutex": "<mutex>",
    "std::lock_guard": "<mutex>",
    "std::chrono": "<chrono>",
    "std::numeric_limits": "<limits>",
    "std::string": "<string>",
    "std::wstring": "<string>",
    "std::move": "<utility>",
    "std::pair<": "<utility>",
    "std::error_code": "<system_error>",
    "std::string_view": "<string_view>",
}

ROOT = Path(__file__).resolve().parent.parent
# Captures the whole token, brackets included ("<string>" or "\"azy/core/x.hpp\""),
# so a requirement written as "<string>" can be compared directly.
INCLUDE_RE = re.compile(r'#include\s+(<[^>]+>|"[^"]+")')


def includes_of(path: Path) -> list[str]:
    return INCLUDE_RE.findall(path.read_text(encoding="utf-8", errors="replace"))


def resolve(name: str, from_dir: Path) -> Path | None:
    bare = name[1:-1]  # drop the brackets
    for candidate in (ROOT / "include" / bare, ROOT / bare, from_dir / bare):
        if candidate.exists():
            return candidate
    return None


def reachable_headers(path: Path, seen: set[Path] | None = None) -> set[Path]:
    """Every project header reachable from `path` through its own includes."""
    seen = set() if seen is None else seen
    for name in includes_of(path):
        target = resolve(name, path.parent)
        if target is None or target.resolve() in seen:
            continue
        seen.add(target.resolve())
        reachable_headers(target, seen)
    return seen


def uses_symbol(path: Path, symbol: str) -> bool:
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if stripped.startswith(("//", "*", "/*")):
            continue
        if symbol in line:
            return True
    return False


def main() -> int:
    files = sorted(set(ROOT.glob("src/**/*.cpp")) | set(ROOT.glob("include/**/*.hpp")))
    problems: list[str] = []
    for path in files:
        available = set(includes_of(path))
        for header in reachable_headers(path):
            available |= set(includes_of(header))
        for symbol, header in REQUIREMENTS.items():
            if header not in available and uses_symbol(path, symbol):
                problems.append(f"{path.relative_to(ROOT)}: uses {symbol!r} but does not include {header}")

    if problems:
        print("include hygiene: the following files rely on a transitive include:")
        for problem in problems:
            print(f"  {problem}")
        print(f"\n{len(problems)} problem(s). Add the header to the file that uses the symbol.")
        return 1

    print(f"include hygiene: {len(files)} files checked, no transitive dependency found")
    return 0


if __name__ == "__main__":
    sys.exit(main())
