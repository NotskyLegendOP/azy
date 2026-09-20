#!/usr/bin/env bash
# Runs everything that can be verified without Windows:
#
#   1. build + run the portable core unit tests natively
#   2. cross-compile the complete Windows application (MSVC-free check that the
#      whole Win32 layer still compiles and links, with the GUI subsystem set)
#
#   ./scripts/verify.sh
#
# Manual Windows-side test procedures live in docs/TESTING.md.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== 1/2 core unit tests (native) =="
cmake -S "$root" -B "$root/build-tests" -DAZY_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug > /dev/null
cmake --build "$root/build-tests" --parallel > /dev/null
ctest --test-dir "$root/build-tests" --output-on-failure

echo
echo "== 2/2 Windows cross-compile =="
"$root/scripts/build-windows.sh" "$root/build-zig"

echo
echo "All checks passed."
