# Azy Skin — Building

Azy Skin has **no third-party dependencies**: CMake, a C++17 compiler and the
Windows SDK (or the MinGW-w64 cross headers) are all that is required. GDI+,
DWM, WMI and the shell APIs are part of Windows.

```
CMakeLists.txt              azy_core (portable) + AzySkin (WIN32 exe) + azy_core_tests
cmake/toolchain-zig-mingw.cmake   optional cross-compile toolchain
scripts/zig-cxx.sh          compiler wrapper used by the cross toolchain
```

---

## Windows (the supported build)

Requirements:

* CMake 3.20 or newer
* Visual Studio 2019/2022 with the Desktop C++ workload, **or** MinGW-w64
* Windows SDK 10 (for `dwmapi.h`, `gdiplus.h`, `wbemidl.h`, `winver.h`)

```powershell
powershell -ExecutionPolicy Bypass -File scripts\build-windows.ps1
# optional: -Arch ARM64 -Config Debug -SkipTests -BuildDir build-x64
```

or directly:

```powershell
cmake -S . -B build -DAZY_BUILD_TESTS=ON
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Output: `build\Release\AzySkin.exe` (~440 KB, GUI subsystem, DPI-aware,
manifest + icon + version info embedded).

### Build options

| Option | Default | Meaning |
|---|---|---|
| `AZY_BUILD_TESTS` | `ON` | Build `azy_core_tests` (portable core unit tests) |
| `AZY_EMBED_MANIFEST` | `ON` | Compile `resources/azy_skin.rc` (per-monitor-v2 DPI, common controls v6, asInvoker, icon, version info). When off, the runtime DPI-awareness call is the fallback |
| `AZY_WARNINGS` | `ON` | Strict warning set (`/W4` on MSVC, `-Wall -Wextra` elsewhere) |

---

## Cross-compiling from Linux/macOS (verification only)

CI uses the clang bundled in the `ziglang` pip package, which ships the MinGW-w64
headers and the Windows import libraries:

```bash
python3 -m pip install ziglang cmake
./scripts/verify.sh          # core tests natively + full Windows cross-compile
```

or the cross build alone:

```bash
./scripts/build-windows.sh   # -> build-zig/AzySkin.exe
```

This produces a genuine PE32+ x64 GUI executable (verified in CI: machine
`0x8664`, subsystem `2`, no console window) and is a strong smoke test that the
whole Win32 layer compiles and links.

**What the cross build does not cover:**

* No resource compiler exists in that toolchain, so the build runs with
  `-DAZY_EMBED_MANIFEST=OFF`: the manifest, icon and version resource are *not*
  embedded, and the runtime fallbacks are what get exercised. Resource
  compilation is covered by the MSVC job in CI instead.
* Nothing is executed: PE files are compiled and linked, not run. Behaviour must
  be verified on Windows (see [`TESTING.md`](TESTING.md)).

---

## Packaging

```powershell
powershell -ExecutionPolicy Bypass -File scripts\package.ps1
# requires Inno Setup 6 (choco install innosetup)
```

Produces `dist\AzySkin-1.0.0-setup.exe` from `packaging\AzySkin.iss`.

Installer properties that matter:

* **Per-user** (`PrivilegesRequired=lowest`) — no UAC prompt, and Azy runs under
  the same user as Premiere, which is required for it to observe Premiere's
  window at all.
* Installs into `{autopf}\Azy Skin`; **never** into an Adobe folder.
* `AppMutex=AzySkin.SingleInstance.7f2a1c94` matches the mutex the application
  creates, so Setup asks a running instance to close and restarts it afterwards
  rather than overwriting a running executable.
* Wizard choices are handed over as `%LOCALAPPDATA%\Azy Skin\install-defaults.ini`;
  Azy applies and deletes it on first launch (so a re-install never overrides a
  later user change).
* The uninstaller deletes only Azy's files, `%LOCALAPPDATA%\Azy Skin` and the
  `HKCU\...\Run\Azy Skin` value — never Adobe content, preferences or projects.

---

## Icon

The icon is generated, not hand-drawn, and **is tracked in the repository**
(`resources/azy_skin.ico`) so building does not require Python:

```bash
python3 tools/make_icon.py     # 16/24/32/48/64 BMP frames + 128/256 PNG frames
```

The same mark is drawn at runtime by `src/win32/ui/app_icon.cpp` when no embedded
resource icon is available (for example in the cross build).

---

## Verifying a build

```bash
# everything that can be checked without Windows
./scripts/verify.sh
```

```powershell
# on Windows: core tests + the manual matrix
ctest --test-dir build -C Release --output-on-failure
# then work through docs/TESTING.md
```

Quick sanity checks for a fresh Windows build:

```powershell
# subsystem must be 2 (GUI), not 3 (console)
python -c "import struct;d=open(r'build\Release\AzySkin.exe','rb').read();pe=struct.unpack_from('<I',d,0x3c)[0];print(struct.unpack_from('<H',d,pe+24+68)[0])"

# it should start, log, and appear in the tray with nothing else running
.\build\Release\AzySkin.exe --debug
Get-Content "$env:LOCALAPPDATA\Azy Skin\azy.log" -Tail 20
```

---

## Continuous integration

`.github/workflows/build.yml` runs four jobs:

| Job | Runner | Purpose |
|---|---|---|
| `core-tests` | ubuntu | build + run the portable core tests |
| `windows-cross-compile` | ubuntu | compile/link the entire Win32 application; assert the artifact is PE32+ x64 with GUI subsystem |
| `windows-msvc` | windows | the supported build, including the resource compiler; runs the tests; uploads the executable |
| `installer` | windows | builds the Inno Setup installer and uploads it |
