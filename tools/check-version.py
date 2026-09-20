#!/usr/bin/env python3
"""Checks that every copy of Azy Skin's version number agrees.

The number lives in five places because five different tools need it:

  CMakeLists.txt                 project(... VERSION ...)   - the build, and the
                                                              runtime banner via
                                                              AZY_VERSION_STRING
  include/azy/core/version_string.hpp  fallback for manual compiles
  packaging/AzySkin.iss          #define AppVersion          - the installer
  resources/azy_skin.rc          VERSIONINFO                - Explorer/Properties
  .github/workflows/release.yml  workflow_dispatch default  - manual releases

They are cheap to keep in step and expensive to get wrong: a mismatch produces a
release whose installer, banner and file properties disagree, and the release
workflow refuses to publish at all when the tag and the .iss differ.

    python3 tools/check-version.py

Exits non-zero and prints every disagreement it found.
"""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def find(pattern: str, text: str, label: str) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        print(f"  ! could not read the version from {label}")
        sys.exit(1)
    return match.group(1)


def main() -> int:
    sources: list[tuple[str, str, str]] = []

    cmake = read("CMakeLists.txt")
    sources.append(("CMakeLists.txt", "project(... VERSION ...)", find(r"^\s*VERSION\s+(\d+\.\d+\.\d+)", cmake, "CMakeLists.txt")))

    header = read("include/azy/core/version_string.hpp")
    sources.append(
        (
            "include/azy/core/version_string.hpp",
            'AZY_VERSION_STRING fallback',
            find(r'#define\s+AZY_VERSION_STRING\s+"(\d+\.\d+\.\d+)"', header, "version_string.hpp"),
        )
    )

    iss = read("packaging/AzySkin.iss")
    sources.append(("packaging/AzySkin.iss", "#define AppVersion", find(r'#define\s+AppVersion\s+"([^"]+)"', iss, "AzySkin.iss")))

    rc = read("resources/azy_skin.rc")
    rc_version = find(r'VALUE\s+"ProductVersion",\s+"(\d+\.\d+\.\d+)\.\d+"', rc, "azy_skin.rc")
    sources.append(("resources/azy_skin.rc", "ProductVersion", rc_version))

    workflow = read(".github/workflows/release.yml")
    sources.append(
        (
            ".github/workflows/release.yml",
            "workflow_dispatch default",
            find(r'^\s*default:\s*"(\d+\.\d+\.\d+)"', workflow, "release.yml"),
        )
    )

    problems = 0
    expected = sources[0][2]
    for path, what, value in sources:
        status = "ok" if value == expected else "MISMATCH"
        if value != expected:
            problems += 1
        print(f"  {status:8} {value:10} {path}  ({what})")

    # The resource file also carries the four-component form; check it too.
    match = re.search(r"FILEVERSION\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", rc)
    if not match:
        print("  ! could not read FILEVERSION from resources/azy_skin.rc")
        return 1
    four = ".".join(match.groups())
    if four != f"{rc_version}.0":
        print(f"  MISMATCH FILEVERSION {four} != project VERSION {rc_version}.0")
        problems += 1

    if problems:
        print(f"\nversion mismatch: {problems} file(s) disagree with {expected}")
        return 1
    print(f"\nversion {expected} agrees everywhere")
    return 0


if __name__ == "__main__":
    sys.exit(main())
