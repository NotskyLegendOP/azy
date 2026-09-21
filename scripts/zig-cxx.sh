#!/bin/sh
# Wrapper that lets CMake treat the zig-packaged clang as a Windows cross
# compiler. Used by scripts/build-windows.sh and by CI so the Windows build can
# be compiled and linked on a Linux machine; end users on Windows should build
# with MSVC or MinGW directly (see docs/BUILDING.md).
#
# Two MinGW driver details are translated here because the zig driver does not
# implement them itself:
#   -mwindows  ->  -Wl,--subsystem,windows   (GUI subsystem, no console window)
#   -municode  is passed through (it works, selecting the wWinMain entry point)

target="${AZY_ZIG_TARGET:-x86_64-windows-gnu}"

args=""
for arg in "$@"; do
    case "$arg" in
        -mwindows) args="$args -Wl,--subsystem,windows" ;;
        *) args="$args $arg" ;;
    esac
done

exec python3 -m ziglang c++ -target "$target" $args
