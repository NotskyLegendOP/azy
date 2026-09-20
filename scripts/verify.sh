#!/usr/bin/env bash
# Runs everything that can be verified without Windows:
#
#   1. check include hygiene (the failure mode that only shows up on MSVC)
#   2. check that every copy of the version number agrees
#   3. build + run the portable core unit tests natively
#   4. cross-compile the complete Windows application (MSVC-free check that the
#      whole Win32 layer still compiles and links, with the GUI subsystem set)
#   5. inspect the resulting executable: PE type, GUI subsystem, and the embedded
#      manifest/icon/version resources
#
#   ./scripts/verify.sh
#
# Manual Windows-side test procedures live in docs/TESTING.md.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== 1/5 include hygiene =="
python3 "$root/tools/check-includes.py"

echo
echo "== 2/5 version strings =="
python3 "$root/tools/check-version.py"

echo
echo "== 3/5 core unit tests (native) =="
cmake -S "$root" -B "$root/build-tests" -DAZY_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug > /dev/null
cmake --build "$root/build-tests" --parallel > /dev/null
ctest --test-dir "$root/build-tests" --output-on-failure

echo
echo "== 4/5 Windows cross-compile =="
"$root/scripts/build-windows.sh" "$root/build-zig"

echo
echo "== 5/5 inspect the executable =="
python3 "$root/tools/inspect-pe.py" "$root/build-zig/AzySkin.exe"

echo
echo "All checks passed."
