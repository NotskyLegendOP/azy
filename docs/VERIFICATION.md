# Verification status

Per-feature evidence for **v1.2.2** (the audit's fixes; the logic is unchanged
from v1.2.1 apart from those fixes). This file answers one question only:
*what has actually been proven, and how?*

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
# native logic tests (13 groups)
cmake -S . -B build-tests -DAZY_BUILD_TESTS=ON && cmake --build build-tests --target azy_core_tests && ./build-tests/azy_core_tests

# the six-step gate: include hygiene, version strings, tests, Windows build, exe inspection, board sync
./scripts/verify.sh
```

Latest results: **578 checks, 0 failures**; **`verify.sh` 6/6 green**; Windows
artefact `AzySkin.exe` **560,128 bytes**, Release, x64, zero warnings from
`sources/` + `include/`.

---

## 1. Build, packaging and release

| Feature | Status | Evidence |
| --- | --- | --- |
| Sources compile for Windows x64 (zig + MinGW, `-Werror`-clean) | VERIFIED | `scripts/verify.sh` step 4; zero warnings attributable to `src/` or `include/` |
| Include hygiene (every header self-sufficient) | VERIFIED | `verify.sh` step 1, catches transitive-include use |
| Version agrees across every file (exe, RC, manifest, ISS, README, docs) | VERIFIED | `verify.sh` step 2 + `tools/check-version.py` |
| Shipped exe is a GUI binary with the expected imports | VERIFIED | `verify.sh` step 5 (inspection of the produced PE) |
| Release assets exist and are public for a tagged version | VERIFIED | GitHub Actions run `35517903596` (v1.2.1): `AzySkin-1.2.1-setup.exe` 2,501,365 B, `-x64-portable.zip` 241,142 B, `-source.zip` 323,838 B; `gh release view v1.2.1` → `isDraft:false` |
| Installer *script* is correct (paths, tasks, registry, uninstall) | COMPILED | Reviewed line by line; built successfully by the CI Inno job |
| Installer runs, installs, starts, uninstalls cleanly | UNVERIFIED | No Windows machine; Inno Setup cannot run here. Nobody has clicked through Setup. |
| Portable zip runs from a folder | UNVERIFIED | Never executed |
| Code signing | UNVERIFIED (and absent by design) | No certificate; SmartScreen warning expected |
| Progress board matches the repository | VERIFIED | `verify.sh` step 6 (`tools/progress.py --check`) |

## 2. Pure logic — the 578 native checks

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
