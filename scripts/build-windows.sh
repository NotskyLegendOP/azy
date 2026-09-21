#!/usr/bin/env bash
# Cross-compiles the Windows application on Linux/macOS using the clang bundled
# with the `ziglang` Python package (which ships the MinGW-w64 headers and the
# Windows import libraries).
#
# This is the same command CI uses, so a change that breaks the Windows build is
# caught without a Windows machine:
#
#   python3 -m pip install ziglang cmake
#   ./scripts/build-windows.sh
#
# The result (build-zig/AzySkin.exe) is a genuine Windows GUI executable; see
# docs/BUILDING.md for what the cross build does and does not cover.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${1:-$root/build-zig}"
target="${AZY_ZIG_TARGET:-x86_64-windows-gnu}"

export AZY_ZIG_TARGET="$target"

cmake -S "$root" -B "$build_dir" \
    -DCMAKE_TOOLCHAIN_FILE="$root/cmake/toolchain-zig-mingw.cmake" \
    -DCMAKE_BUILD_TYPE=Release \
    -DAZY_BUILD_TESTS=OFF

cmake --build "$build_dir" --parallel

echo
echo "== Artifact =="
ls -l "$build_dir/AzySkin.exe"
