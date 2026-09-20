#!/usr/bin/env bash
# Runs everything that can be verified without Windows:
#
#   1. check include hygiene (the failure mode that only shows up on MSVC)
#   2. check that every copy of the version number agrees
#   3. check that the overlay shader and the C++ constant buffer that feeds it
#      still agree (a mismatch there is only visible as wrong pixels)
#   4. build + run the portable core unit tests natively
#   5. cross-compile the complete Windows application (MSVC-free check that the
#      whole Win32 layer still compiles and links, with the GUI subsystem set)
#   6. inspect the resulting executable: PE type, GUI subsystem, and the embedded
#      manifest/icon/version resources
#   7. check that the generated progress board still matches docs/progress.json
#
#   ./scripts/verify.sh
#
# Manual Windows-side test procedures live in docs/TESTING.md.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== 1/7 include hygiene =="
python3 "$root/tools/check-includes.py"

echo
echo "== 2/7 version strings =="
python3 "$root/tools/check-version.py"

echo
echo "== 3/7 overlay shader + constant buffer =="
python3 "$root/tools/check-overlay.py"

echo
echo "== 4/7 core unit tests (native) =="
cmake -S "$root" -B "$root/build-tests" -DAZY_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug > /dev/null
cmake --build "$root/build-tests" --parallel > /dev/null
ctest --test-dir "$root/build-tests" --output-on-failure

echo
echo "== 5/7 Windows cross-compile =="
"$root/scripts/build-windows.sh" "$root/build-zig"

echo
echo "== 6/7 inspect the executable =="
python3 "$root/tools/inspect-pe.py" "$root/build-zig/AzySkin.exe"

echo
echo "== 7/7 progress board in sync =="
python3 "$root/tools/progress.py" --check

echo
echo "All checks passed."
