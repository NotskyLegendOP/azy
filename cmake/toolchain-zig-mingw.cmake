# Cross-compile toolchain: Windows x64 target using the clang bundled with the
# `ziglang` Python package (which ships the MinGW-w64 headers and the Windows
# import libraries).
#
# This is what CI uses to verify that the Windows build compiles on every push
# without needing a Windows runner. End users on Windows should build with MSVC
# or MinGW instead - see docs/BUILDING.md.
#
#   python -m pip install ziglang cmake
#   cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-zig-mingw.cmake
#   cmake --build build

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

find_program(AZY_ZIG_PYTHON NAMES python3 python REQUIRED)

# A tiny wrapper so CMake can invoke zig as an ordinary C++ driver.
set(_azy_wrapper "${CMAKE_CURRENT_LIST_DIR}/../scripts/zig-cxx.sh")
set(CMAKE_CXX_COMPILER "${_azy_wrapper}")
set(CMAKE_C_COMPILER "${_azy_wrapper}")
set(AZY_ZIG_TARGET "x86_64-windows-gnu" CACHE STRING "zig target triple" FORCE)

# No resource compiler in this toolchain: the manifest is skipped and the
# application falls back to its runtime DPI-awareness call.
set(CMAKE_RC_COMPILER "" CACHE FILEPATH "" FORCE)

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
