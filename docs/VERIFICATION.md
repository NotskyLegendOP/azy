# Verification status

## Round 9 (2.0.0) verification status

The rebuild replaces the skin: what was verified for v1.3.0 about the ring, the
sheet and the DWM frame treatment no longer describes this code. The current state:

* **Statically verified** — `scripts/verify.sh` (7/7): include hygiene, version
  strings, the shader↔`MirrorParams` contract check (including a mutation test of
  the structural checks), 747 native assertions, the Windows cross-compile, and PE
  resource inspection.
* **Verified by construction, not by running** — the media pass-through delta
  0.0000 and the appearance numbers quoted in
  [`AZY_MIRROR_ARCHITECTURE.md`](AZY_MIRROR_ARCHITECTURE.md) §11 come from
  `tools/preview_render.py`, which is a CPU re-implementation of the shader, not a
  screenshot.
* **Unverified — RUNTIME UNVERIFIED** — everything that needs a real Premiere Pro on
  Windows: capture start and frame delivery, latency and smoothness, click-through,
  focus, move/resize/minimise/fullscreen sync, multi-monitor and DPI behaviour, idle
  CPU and memory, and whether the panel model matches a real workspace.
* **HIGH RISK until then** — the HLSL has never been compiled anywhere in this
  environment (no shader compiler is obtainable here). A shader error would show up
  as "nothing on screen" with a logged reason, never as a broken Premiere, but it
  would still be a dead end until the first Windows run.

Per-feature evidence for **v1.3.0** (the duplicate window overlay; the static path is
unchanged from v1.2.3). This file answers one question only: *what has actually been
proven, and how?*

Three levels are used, and nothing is promoted between them without evidence:

| Level | Meaning |
| --- | --- |
| **VERIFIED** | A machine checked it. Either an automated test in this repository, or a build/inspection step in `scripts/verify.sh`, or a completed GitHub Actions run. The evidence is named. |
| **COMPILED** | The code is cross-compiled into the shipped `AzySkin.exe`, reviewed line by line, and consistent with the documented contract — but has never executed. This is *not* verification. |
| **UNVERIFIED** | Neither of the above. Either no Windows machine has run it, or it depends on Premiere Pro, or it needs human eyes. |

**"Compiles" is not "works".** Most of Azy is Windows API code that cannot run
in this development environment, so most of Azy is COMPILED at best.

Evidence commands, all reproducible:

```sh
# native logic tests (15 groups)
cmake -S . -B build-tests -DAZY_BUILD_TESTS=ON && cmake --build build-tests --target azy_core_tests && ./build-tests/azy_core_tests

# the seven-step gate: include hygiene, version strings, overlay contracts, tests,
# Windows build, exe inspection, board sync
./scripts/verify.sh

# the overlay's own contract check on its own
python3 tools/check-overlay.py
```

Latest results: **649 checks, 0 failures**; **`verify.sh` 7/7 green**; Windows
artefact `AzySkin.exe` **614,912 bytes** (61.4 % of the 1 MB budget), Release, x64,
zero warnings from `src/` + `include/`.

---

## 1. Build, packaging and release

| Feature | Status | Evidence |
| --- | --- | --- |
| Sources compile for Windows x64 (zig + MinGW, warning-clean) | VERIFIED | `scripts/verify.sh` step 5; zero warnings attributable to `src/` or `include/` |
| The same sources compile and **link** with MSVC on Windows (the supported toolchain, incl. the resource compiler) | VERIFIED | GitHub Actions `build` workflow, `windows build (MSVC)` job on this commit — the job that caught the `IID_*` link failure the cross-compile could not see |
| Include hygiene (every header self-sufficient) | VERIFIED | `verify.sh` step 1, catches transitive-include use |
| Version agrees across every file (exe, RC, manifest, ISS, README, docs) | VERIFIED | `verify.sh` step 2 + `tools/check-version.py` |
| Shipped exe is a GUI binary with the expected imports | VERIFIED | `verify.sh` step 5 (inspection of the produced PE) |
| Release assets exist and are public for a tagged version | VERIFIED | v1.3.0, GitHub Actions runs `35526240388` / `35526449456`: the three assets (`-setup.exe`, `-x64-portable.zip`, `-source.zip`) are attached; `gh release view v1.3.0` → `isDraft:false`, `isPrerelease:false`. Sizes are quoted from the release page rather than here, because they change by a few bytes whenever a document changes. |
| The MSVC job that builds those assets runs on every push | VERIFIED | `build` workflow, run `35526005507`: `core unit tests`, `windows build (MSVC)`, `installer` and `windows cross-compile` all green |
| Installer *script* is correct (paths, tasks, registry, uninstall) | COMPILED | Reviewed line by line; built successfully by the CI Inno job |
| Installer runs, installs, starts, uninstalls cleanly | UNVERIFIED | No Windows machine; Inno Setup cannot run here. Nobody has clicked through Setup. |
| Portable zip runs from a folder | UNVERIFIED | Never executed |
| Code signing | UNVERIFIED (and absent by design) | No certificate; SmartScreen warning expected |
| Progress board matches the repository | VERIFIED | `verify.sh` step 7 (`tools/progress.py --check`) |

## 2. Pure logic — the 649 native checks

| Feature | Status | Evidence |
| --- | --- | --- |
| Version parsing/comparison, compatibility ranges, build keys | VERIFIED | `test_version`, `test_build_key`, `test_compat`, `test_product` |
| Theme maths: charcoal ramp, hairlines, bezel, veil alpha, accent, glow, radius clamp, "Original" = fully transparent | VERIFIED | `test_theme`, `test_design_tokens` |
| DPI geometry: DIP→px at 100/125/150/175/200 %, corner radius clamping | VERIFIED | `test_dpi_geometry` |
| Panel map: ratios per workspace, minimum sizes, `usable` contract, unknown workspace key, meters clamp | VERIFIED | `test_panel_map` |
| Settings: parse, validate, INI round-trip, unknown keys preserved | VERIFIED | `test_settings` |
| Failure tracker / Safe Mode thresholds and window | VERIFIED | `test_failure_tracker` |
| String/format helpers, no unbounded growth | VERIFIED | `test_strings` |
| Layout safety: nothing drawn outside its surface, degenerate sizes rejected | VERIFIED | `test_layout_safety`, `test_ring_layout` |
| Overlay capture mapping: the sub-rectangle of a maximized window's capture, refusal to draw when the geometry does not intersect, panel clipping into window-local pixels, pass-through ordering (Program first), hairlines excluded next to the picture regions, shader packing and zero padding, `overlay_rect` for windowed/maximized/fullscreen | VERIFIED | `test_capture_math` (new in v1.3.0) |
| Duplicate window style: visible at the shipped defaults, sliders move it, performance mode keeps the structure and drops the GPU extras, rounded corners off = square, Original theme = no duplicate at all, neutral accent = no hue | VERIFIED | `test_overlay_style` (new in v1.3.0) |
| The shader's constant buffer and the C++ struct agree (members, order, sizes, slot counts, no `float3`, 272 bytes by the HLSL packing rules) | VERIFIED | `verify.sh` step 3, `tools/check-overlay.py`. The checker was itself tested by breaking the shader twice (renamed member, `float3`) and confirming it failed both times. |
| Nothing calls the monitor form of the capture API; the own-process guard exists; the desktop window is never captured | VERIFIED | `verify.sh` step 3 (`tools/check-overlay.py` greps `src/` and the capture module) |
| The duplicate window keeps the styles that make it click-through, non-activating and never topmost, and still answers `HTTRANSPARENT` | VERIFIED | `verify.sh` step 3; mutation-tested by dropping `WS_EX_LAYERED`, adding `WS_EX_TOPMOST` and removing `HTTRANSPARENT` on purpose |

## 2b. The duplicate window overlay (COMPILED — nothing runtime-verified)

Every row here is **COMPILED** unless stated otherwise. This is the honest status of
the round-8 architecture: the code exists, builds, links and passes every check that
can run without Windows and Premiere - and not one of its runtime behaviours has
been observed.

| Feature | Status | Evidence / what is missing |
| --- | --- | --- |
| The capture/composition technique is achievable and is the best available (§34) | VERIFIED (by argument, not by test) | `docs/AZY_OVERLAY_ARCHITECTURE.md` §1.3: Windows Graphics Capture chosen; Desktop Duplication, DWM thumbnails, GDI capture and injection rejected with their reasons. The reasoning is reviewable; the runtime behaviour is not. |
| The WinRT capture ABI is declared correctly (IIDs, vtable order, `CreateFreeThreaded`, cursor capture off) | COMPILED | `include/azy/win32/capture/wgc_abi.hpp`, with the source of every identifier in a comment. **This is the highest risk in the feature**: a wrong vtable slot would not fail cleanly. It has never been executed. |
| A capture can be created for a Premiere window and delivers frames | UNVERIFIED | Needs Windows + Premiere |
| The capture's pixel size matches the window rectangle at 100–200 % DPI | UNVERIFIED | Nothing documents a guarantee; the mismatch is reported in the debug overlay rather than hidden |
| The mirror lines up with the real window (UV mapping) | COMPILED (math VERIFIED, alignment UNVERIFIED) | `test_capture_math` proves the arithmetic; whether it *lines up* needs eyes |
| The Program Monitor's footage is not darkened | COMPILED | The shader's pass-through loop, the rectangles from the panel model, `test_capture_math` for the rectangles |
| Click-through, no focus, no keystrokes | COMPILED | `WS_EX_LAYERED`/`TRANSPARENT`/`NOACTIVATE`/`TOOLWINDOW`, `HTTRANSPARENT`, `MA_NOACTIVATE`, and no keyboard hook anywhere in the source; the styles are held in place by the checker. Whether a real click lands on Premiere is still UNVERIFIED — that needs the machine. |
| Above Premiere, below other applications, never topmost | COMPILED | `z_order_anchor()` + `SetWindowPos` with the window in front of Premiere as `hWndInsertAfter`; no `HWND_TOPMOST` anywhere |
| No recursive/infinite mirror | VERIFIED (structurally) + UNVERIFIED (visually) | Window capture only, the monitor form is banned by `tools/check-overlay.py`, and the capture refuses Azy's own process. That makes nesting impossible *by construction*; the visual check is scenario 2.1 of the test plan. |
| Geometry sync (move, resize, maximize, minimize, DPI, monitors) | COMPILED | `overlay_rect` (tested), the tracker's event stream, `ResizeBuffers` in place |
| Pacing (idle/active) and zero cost while hidden | COMPILED | The timer is re-armed only on a pacing change and killed while hidden |
| All GPU resources are released on suspend/exit/Premiere-close | COMPILED | `teardown_overlay()`, `WindowCapture::stop()` owning the worker thread and the textures |
| Failure containment (unsupported host, device failure, item closed, device lost) | COMPILED | `docs/AZY_OVERLAY_ARCHITECTURE.md` §7; the ring and veil are never stopped by an overlay failure |
| Measured CPU/GPU/memory while mirroring | UNVERIFIED | Not measured anywhere; no number is claimed |

## 3. Detection and monitoring (Win32 — COMPILED only)

| Feature | Status | Evidence |
| --- | --- | --- |
| Finds Premiere dynamically, never a hardcoded path | COMPILED | `premiere_probe.cpp` matches only `adobe premiere pro[ beta\| headless].exe` by name via a WMI/toolhelp scan; the audit confirmed no literal path anywhere in `src/` |
| Does not attach to Media Encoder / After Effects / Photoshop | COMPILED | Exe allowlist above; audited |
| Detects Process Create/Terminate events | COMPILED | `PremiereDetector` WMI event sink; audited |
| Detects window create/move/resize and foreground change | COMPILED | `SetWinEventHook` ranges; audited |
| Idle cost is near zero — no polling loop | COMPILED | Audit: `pump()` returns immediately when no scan is pending; the z-order re-assert is gated on `foreground_dirty \|\| changed` (this was a defect, fixed); `SkinEngine::apply` compares a `VisualKey` and repaints only on a real change, so the 1 Hz recovery-net timer does not redraw once per second |
| Process is opened read-only | VERIFIED | `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)` only — statically confirmed; no write handle exists |
| No injection, hooks, thread attach, `SendInput` | VERIFIED | Repository-wide audit: no `CreateRemoteThread`, `SetWindowsHookEx`, `AttachThreadInput`, `SendInput`, `WriteProcessMemory` |
| Detects Premiere starting and exits cleanly when it stops | COMPILED | `Stopped` path reverts the frame, destroys the surface, clears the tracker and the watched pid |
| Tracks a Beta→release switch (or a restart) without leaking | COMPILED | New pid/window → revert + rebind; audited |
| Elevated Premiere is detected and warned about | COMPILED | Integrity comparison; the warning text is written to the log and the window |

## 4. The skin itself (Win32/GDI+/DWM — COMPILED only)

| Feature | Status | Evidence |
| --- | --- | --- |
| DWM dark window frame on Premiere's window (Windows 11 22000+) | COMPILED | `dwm_composer.cpp`, official DWM attributes only |
| Click-through composition surface (all four ex-styles required) | COMPILED | Audit confirmed `WS_EX_LAYERED \| WS_EX_TRANSPARENT \| WS_EX_NOACTIVATE \| WS_EX_TOOLWINDOW` are all required before the window is shown |
| Never steals focus or input | COMPILED | Same styles; no activation call anywhere; `SWP_NOACTIVATE` used for every placement |
| Ring: 1 px hairline + 1 px lighter bezel + inward falloff, 6/8/10 DIP corners | COMPILED | `gdiplus_renderer.cpp`; geometry asserted by `test_ring_layout` for the values, UNVERIFIED for the result |
| Whole-window veil, one constant alpha, no bitmap | COMPILED | `overlay_veil.cpp`; no per-pixel work exists |
| Static — no animation, no timer-driven redraw | VERIFIED (by construction) | No animation code path exists; the `animations` setting is inert (see `KNOWN_LIMITATIONS.md`) |
| Follows move, resize, maximise, restore, fullscreen | COMPILED | `SetWinEventHook` handlers, no polling |
| Multi-monitor, never assumes the primary monitor | COMPILED | Geometry comes from the anchor window's own rectangle and monitor; audited |
| 100–200 % DPI | COMPILED | Per-window DPI resolution; maths VERIFIED, the visual result not |
| Suspend on minimise, resume on restore | COMPILED | Default on; audited |
| Instant global ON/OFF from the tray | COMPILED | Tray command → engine revert/apply |
| Zero residual CPU/GPU after Exit | UNVERIFIED | Cannot be measured without Windows; the design has no timer, no thread and no capture once released |
| Debug overlay draws the modelled panel regions over the live window | COMPILED | Fixed during this audit: it used screen coordinates inside its own window, and the map was built from the frame rectangle instead of the client area. Both fixed; **nobody has looked at the result.** |

## 5. Interface and behaviour

| Feature | Status | Evidence |
| --- | --- | --- |
| Tray menu: Skin Enabled, Theme, Settings, Start with Windows, Suspend Skin, Exit | COMPILED | `tray.cpp`, fixed command ids; audited |
| Settings window: dark, 4 sections, never steals the skin's state | COMPILED | `settings_window.cpp`; children are destroyed/recreated on DPI change (audited, no leak) |
| Every control in Settings does something | VERIFIED | Audit cross-checked every control id against its handler; the one exception (`animations`) is now disabled and labelled rather than silently inert |
| Settings apply immediately (no restart) | COMPILED | `push_settings()` → controller → engine request |
| Settings persist and reload | VERIFIED | `test_settings` (INI round-trip, including `ui_profile`, `debug_mode`) |
| Start with Windows (HKCU Run, no admin) | COMPILED | Registry write/remove audited; never writes HKLM |
| Single instance — a second launch focuses the first | COMPILED | Named mutex + `FindWindow` activation; audited |
| Safe Mode after repeated failures, cleared by opting in | VERIFIED (logic) | `test_failure_tracker` |
| Logging: rotating, lightweight, levelled | VERIFIED (format) | Audit; format is now `YYYY-MM-DD HH:MM:SS [LEVEL] message`, rotation is one file + `.1` |
| The log is *useful* when the skin does not appear | UNVERIFIED | Nobody has read a log from a failing machine |
| Visual appearance: dark, glossy, glassy, premium, subtle | UNVERIFIED | **No runtime screenshot exists.** The user is the only reviewer, on their own machine. |

## 6. Performance targets

| Target | Status | Note |
| --- | --- | --- |
| ~0–1 % CPU idle | UNVERIFIED | No measurement possible here. The design is event-driven with no timer; the audit removed the one idle-path waste it found (a per-sync z-order walk). |
| < 2 % while monitoring | UNVERIFIED | As above |
| < 100 MB RAM | UNVERIFIED | Not measurable. No Chromium, no runtime dependencies, no large buffers in the code. |
| No full-screen rendering, no capture, no high-frequency polling | VERIFIED (by inspection) | No screen capture (`BitBlt`/`PrintWindow`) exists. The only pixel reads are ~40 `GetPixel` probes inside the user-triggered **Check visibility** diagnostic, which hides the veil/ring, flushes one frame, samples and restores — nothing samples the screen on a timer. Monitoring is event-driven (WMI + `SetWinEventHook`); the only timer is a 1 Hz safety net that repaints nothing when the visual key is unchanged. |

## 7. What this table means

- Everything mathematical and data-shaped is genuinely verified, and it is the
  part most likely to be *silently* wrong.
- Everything that touches Windows is COMPILED. The audit found and fixed six real
  defects in exactly that category — which is the strongest argument that the
  remaining risk is concentrated there, not in the logic.
- Nothing about appearance, performance, or behaviour next to Premiere Pro has
  been observed. **The next honest step is one run on the user's machine**, with
  the debug overlay on, and a look at what the log says.

The audit's own conclusions are in [`AZYSKIN_AUDIT.md`](AZYSKIN_AUDIT.md);
the boundary of what the product can ever do is in
[`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md).
