#!/usr/bin/env python3
"""Checks that every copy of Azy Skin's version number agrees.

The number lives in five places, and each one is read by a different tool:

  CMakeLists.txt                 project(... VERSION ...)   the build, and
                                                            AZY_VERSION_STRING
  include/azy/core/version_string.hpp  the fallback used when the sources are
                                       compiled by hand
  packaging/AzySkin.iss          #define AppVersion          the installer
  resources/azy_skin.rc          VERSIONINFO                 Explorer / Properties
  .github/workflows/release.yml  workflow_dispatch default   manual releases

A mismatch is easy to create and expensive to notice: the installer would install
one version over another, the Properties page would disagree with the banner, and
tools/check-version.py exists so that neither can happen quietly. The release
workflow additionally refuses to publish when the tag and the .iss disagree.

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


def find(path: str, pattern: str, text: str) -> str:
    match = re.search(pattern, text, re.MULTILINE)
    if not match:
        print(f"  ! could not read the version from {path}")
        sys.exit(1)
    return match.group(1)


def main() -> int:
    sources: list[tuple[str, str]] = []

    cmake = read("CMakeLists.txt")
    sources.append(("CMakeLists.txt", find("CMakeLists.txt", r"^\s*VERSION\s+(\d+\.\d+\.\d+)", cmake)))

    header = read("include/azy/core/version_string.hpp")
    sources.append(
        (
            "include/azy/core/version_string.hpp",
            find("include/azy/core/version_string.hpp", r'#define\s+AZY_VERSION_STRING\s+"(\d+\.\d+\.\d+)"', header),
        )
    )

    iss = read("packaging/AzySkin.iss")
    sources.append(("packaging/AzySkin.iss", find("packaging/AzySkin.iss", r'#define\s+AppVersion\s+"([^"]+)"', iss)))

    rc = read("resources/azy_skin.rc")
    sources.append(("resources/azy_skin.rc", find("resources/azy_skin.rc", r'VALUE\s+"ProductVersion",\s+"(\d+\.\d+\.\d+)\.\d+"', rc)))

    workflow = read(".github/workflows/release.yml")
    sources.append(
        (
            ".github/workflows/release.yml",
            find(".github/workflows/release.yml", r'^\s*default:\s*"(\d+\.\d+\.\d+)"', workflow),
        )
    )

    expected = sources[0][1]
    problems = 0
    for path, value in sources:
        ok = value == expected
        problems += 0 if ok else 1
        print(f"  {'ok' if ok else 'MISMATCH':8} {value:10} {path}")

    # The resource file carries the four-component form as well.
    four_match = re.search(r"FILEVERSION\s+(\d+),\s*(\d+),\s*(\d+),\s*(\d+)", rc)
    if not four_match:
        print("  ! could not read FILEVERSION from resources/azy_skin.rc")
        sys.exit(1)
    four = ".".join(four_match.groups())
    if four != f"{expected}.0":
        print(f"  MISMATCH {four:10} resources/azy_skin.rc  (FILEVERSION)")
        problems += 1

    if problems:
        print(f"\nversion mismatch: {problems} file(s) disagree with {expected}")
        return 1
    print(f"\nversion {expected} agrees everywhere")
    return 0


if __name__ == "__main__":
    sys.exit(main())
