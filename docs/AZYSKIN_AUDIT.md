# Azy Skin — complete implementation audit

**Version audited:** v1.2.1
**Fixes shipped in:** v1.2.2 (the six code defects in §3, the accuracy fixes in
§3's table, and the dead scratch files removed from `tools/`)
**Audit date:** 2026-09-20
**Scope:** every source file in `src/` and `include/`, the build scripts, the
installer script, the release pipeline, and all documentation.
**Constraint:** the auditor cannot run the application or observe the result.
Behaviour is treated as unproven unless a machine actually checked it.

Companion documents:
[`VERIFICATION.md`](VERIFICATION.md) — what is verified and what is not.
[`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md) — what the product cannot do.

---

## 1. Executive summary

Azy Skin is a ~11,050-line native C++/Win32 application (71 source files, 1,033
lines of tests) that skins Adobe Premiere Pro's window from the outside. It does
not inject, hook, patch, automate, or touch any Adobe file — the audit confirmed
that by inspection of every relevant call site, not by trusting the comments.

**What holds up.** The architectural decisions are sound and were made
consistently: one click-through layered window for the ring, one constant-alpha
surface for the veil, DWM attributes for the frame, `SetWinEventHook` for every
position/state change. The only timer in the program is a 1 Hz safety net that
costs a dozen comparisons per tick (§6). The forbidden
techniques are genuinely absent. The pure logic (theme maths, DPI geometry, panel
map, settings round-trip, failure tracking, version compatibility) is covered by
578 native checks that pass. The build is warning-clean from Azy's own sources.

**What the audit found.** Nine fixes, of which seven are code defects — five in
the Windows layer, which has never been executed and is therefore exactly where
defects accumulate, one in the settings UI and one in the logger. The remaining
two are documentation and tooling accuracy. They were found by reading the code
against the Windows API contract and by cross-checking every settings control
against its consumers; none was found by a test, and none was found by compiling. The most serious is the debug overlay, which drew screen
coordinates inside a window positioned in client coordinates: the whole panel map
was offset by the window's screen position and would have appeared in the wrong
place (or off-screen) on every machine whose window was not near the origin. The
second is that the panel map was derived from the frame rectangle, which includes
the caption and invisible resize borders, so every region below the caption was
shifted down by 30+ px. Both are fixed. **Neither could have been caught by
compiling, and neither would have been visible in the shipped v1.2.1 default
build, because the debug overlay ships off.**

**What remains unproven.** Everything that requires Windows, and everything that
requires eyes: whether the skin actually appears above Premiere in the maximised
state the user reported, whether the colours read the way they should, and
whether the performance targets are met. No runtime screenshot exists and none
could be produced here. This audit therefore cannot certify the visual result.

**Verdict.** The code is clean, the forbidden techniques are absent, the logic is
tested, and the Windows-layer defects that could be found without running the
program have been found and fixed. But the single question the user actually cares
about — *does the skin appear on Premiere Pro on my machine?* — has never been
answered by observation, and the most recent user report (round 3) was that it did
not. On that basis the honest readiness is **READY FOR RUNTIME TESTING**, not
anything higher.

---

## 2. Architecture

```
main.cpp        single instance → settings store → AppController → message pump
   │
   ├─ core/            no Windows headers at all; unit-tested on any platform
   │    version, compat, theme (brand palette + token maths), panel_map,
   │    settings (INI), failure_tracker, log, strings
   │
   ├─ win32/detect/    PremiereProbe (exe allowlist) + PremiereDetector (WMI
   │                   process events)  → "Premiere started / stopped / changed"
   │
   ├─ win32/watch/     EventWatch: SetWinEventHook over
   │                   [kEventSystemForeground … kEventSystemMoveSizeEnd],
   │                   [kEventObjectCreate … kEventObjectLocationChange],
   │                   [kEventObjectCloaked … kEventObjectUncloaked]
   │                   → "the tracked window moved / resized / changed state"
   │
   ├─ win32/track/     WindowTracker: the anchor window, its monitor, its DPI
   │
   ├─ win32/skin/      SkinEngine composes the request from tracker + theme +
   │                   settings and drives three independent layers:
   │                     • dwm_composer      – caption/border colours (DWM attrs)
   │                     • composition_surface – the ring (GDI+, one bitmap)
   │                     • overlay_veil      – the whole-window tint (one alpha)
   │                   plus debug_overlay (off by default)
   │
   └─ win32/ui/        tray, settings window, app icon
```

Design rules the code obeys everywhere:

- **Event-driven only, with one deliberate safety net.** There is no polling loop
  and no thread of Azy's own (`CreateThread`/`std::thread` appear nowhere in
  `src/`): process changes arrive over WMI events, window changes over
  `SetWinEventHook`. One 1 Hz timer exists on the controller's message window as a
  recovery net for a notification Windows failed to deliver; a tick evaluates the
  performance decision and compares the visual key, and repaints only if something
  actually changed. See §6 for its measured-by-inspection cost.
- **Own windows only.** Azy renders into windows it created, in its own process,
  and styles Premiere's window only through documented DWM attributes.
- **Read-only toward Adobe.** `OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION)`
  is the only handle opened on Premiere; there is no write handle anywhere.
- **Forgiving by default.** Every layer can fail independently; a failure
  degrades the look, never Premiere's behaviour, and is written to the log.
- **Data, not code.** Themes, presets, panel ratios and design tokens are tables
  (`theme.cpp`, `panel_map.cpp`) that the settings UI, the INI and the tests all
  read, so a change in one place cannot disagree with the others.

---

## 3. Critical problems

No defect of class **P0** (data loss, crash, security, or anything that could
harm Premiere or the user's system) was found. Four defects were classified P1:
three of them (P1-1, P1-2, P1-3) damage a core promise, and the fourth (P1-4)
would have made a user distrust the settings window for no reason. All four are
fixed.

### P1-1 — The debug overlay drew screen coordinates inside its own window
`src/win32/skin/debug_overlay.cpp`

`WM_PAINT` used the panel rectangles *as returned by the panel map*, which are
screen coordinates, and drew them into a window whose origin sits at the tracked
frame's corner. `FillRect`/`DrawTextW` interpret their rectangles as
client-relative, so every rectangle was displaced by the window's screen
position. On a maximised window (≈ `(0,0)` plus the 8 px resize overhang) the
error approaches the caption height; on a window at `(1200, 400)` the entire
overlay drew outside its own client area and was clipped away.

**Effect:** the debug overlay — the tool built specifically to let the user
verify the panel model — would have shown nothing useful on exactly the machine
that needed it, and would have looked like a fault in the panel map rather than in
the overlay.

**Fixed:** `DebugOverlay` now records the frame origin when it is presented
(`origin_`), and `WM_PAINT` subtracts it before drawing. Panel rectangles stay in
screen space (the honest coordinate system for them) and are converted once, at
the point of drawing.

### P1-2 — The panel map was built from the frame rectangle instead of the client area
`src/win32/skin/skin_engine.cpp`, `src/win32/os/win_util.{hpp,cpp}`

`GetWindowRect` returns the window rectangle *including* the non-client area:
the caption (≈ 30 px at 100 %, ≈ 45 px at 150 %) plus the invisible resize border
(≈ 8 px at 100 %). The panel map was fed that rectangle, so the "menu bar band"
was drawn across the caption, and every region below it — header, docks, timeline
— was shifted down by roughly the caption height, growing with DPI.

**Effect:** any region work built on the map (and the debug overlay) would have
been consistently offset, in a way that looks like a modelling error but is
actually an origin error.

**Fixed:** `win_util::window_client_rect(HWND, Rect&)` was added
(`GetClientRect` + `ClientToScreen`, the documented and DPI-correct way to get a
client area in screen space). The engine builds the map from it and records the
client origin separately from the frame origin, so the ring (which must follow the
frame) and the regions (which must follow the client area) no longer share an
origin by accident.

### P1-3 — Whole sync path re-asserted z-order unnecessarily → up to ~1000 window calls per second at idle
`src/app/app_controller.cpp`

`engine_.reassert_stacking()` ran on every sync. Stacking can only change when
the foreground window changes, but the sync path runs for every window event
(including the ones Premiere generates constantly while the user edits). Each
call walks the window list with `GetWindow`, so a busy edit session — or a
background window generating location events — could drive hundreds to ~1000
`GetWindow` calls per second with no purpose.

**Effect:** a direct threat to the "0–1 % CPU idle" target, and exactly the kind
of idle churn the brief forbids.

**Fixed:** the call is gated on `foreground_dirty || changed`. The z-order is
re-asserted when, and only when, it can have been invalidated.

*(Severity note: classified P1 rather than P2 because it is the one defect that
attacks a stated hard requirement — the idle performance budget.)*

### P1-4 — The animations switch was inert
`src/win32/ui/settings_window.cpp`

Every other control in Settings has a consumer; `appearance.animations` had none.
It was persisted, validated and round-tripped — and read by nothing. The user
could tick it, be told it was on, and see nothing change, with no way to tell
whether the setting or the skin was broken.

**Fixed without removing anything:** the setting and its INI key remain (existing
configurations are never rewritten), and the control is now created **disabled**
with the label *"Fade Azy's own layers (not available — Azy is static)"*, so the
UI states the truth instead of promising motion. Implementing real animation was
rejected: it would contradict the standing "static, no animation" requirement and
would need exactly the repaint loop the brief forbids. Recorded in
[`KNOWN_LIMITATIONS.md`](KNOWN_LIMITATIONS.md) §4.

### Fixed during this audit — the complete list

Rows 1–7 are the code defects — four P1, one P2, two P3; rows 8–9 are
documentation and tooling accuracy.

| # | File(s) | Class | Defect | Fix |
| --- | --- | --- | --- | --- |
| 1 | `debug_overlay.{hpp,cpp}` | P1 | Screen coordinates painted into a client-coordinate window | `origin_` recorded at present, subtracted at paint |
| 2 | `skin_engine.cpp`, `win_util.{hpp,cpp}` | P1 | Panel map built from the frame rectangle (caption + resize border included) | New `window_client_rect()`; map + regions now client-based |
| 3 | `app_controller.cpp` | P1 | Z-order re-asserted on every sync (idle CPU churn) | Gated on `foreground_dirty \|\| changed` |
| 4 | `skin_engine.cpp` | P2 | Detach/suspend left a stale `panels_` map behind | Map cleared in the suspend/remove path |
| 5 | `debug_overlay.{hpp,cpp}`, `skin_engine.cpp` | P3 | Stock GUI font at fixed 16 px spacing — blurry and cramped at 150–200 % | Segoe UI 12 DIP ClearType font, created per DPI, released on destroy; `present()` takes the DPI |
| 6 | `settings_window.cpp` | P1 | Inert "Animations" switch (P1-4 above) | Kept, documented, disabled and relabelled |
| 7 | `src/core/log.cpp` | P3 | Level field was a bare `INFO ` with a trailing space | Bracketed fixed-width `[INFO ]` / `[WARN ]` / `[ERROR]` / `[DEBUG]`, so the log is greppable for levels; columns still align |
| 8 | `tools/preview_render.py` | P3 | The design-preview tool's hand-transcribed palette was indistinguishable from generated ground truth | Docstring now states it is a transcription, that nothing verifies it against `theme.cpp`, and how to regenerate the images |
| 9 | `README.md`, `docs/BUILDING.md`, `docs/PERFORMANCE.md`, `docs/TECHNIQUES.md`, `tools/inspect-pe.py` | P3 | Four documents quoted "~440 KB" for an executable that is 560,128 bytes, and the idle-timer description ("read a few atomics, return") understated what a tick actually does | Sizes corrected to the real number; timer description replaced with what the code does; a 1 MB size budget added to the release-artifact inspection so the number cannot drift again |

---

## 4. Remaining problems

Ordered by class. None of these is fixed by this audit; each is a deliberate
decision or an honest gap.

### P2 — Functional gaps that affect the promise, not the system

1. **No regional treatment ships.** The panel map exists, is unit-tested and
   drives the debug overlay, but the dock separators, header band, timeline band,
   monitor/project/effect-controls bands and the audio-meter region are all still
   open (P5, 5 items). Today's skin is the ring + the veil + the DWM frame. This
   is the biggest remaining distance between the current build and the brief's
   "skinned panels" reading.
2. **The panel map is a model of *default* workspaces.** A customised layout will
   not match it, and Azy cannot read Premiere's layout to find out. The manual UI
   profile selector is the only mitigation. This is a permanent constraint, not a
   temporary one.
3. **Undocked (floating) panels are not skinned** (P6, open). They are separate
   top-level windows; only the anchor window is tracked.
4. **The whole-window veil is the contentious feature.** Round 3 asked for it
   explicitly; it is implemented as the cheapest possible thing (one constant
   alpha). It is uniform by design — Premiere has no per-region treatment yet — so
   a user who expects panel-boundary light falloff will not see any.
5. **Elevated Premiere cannot be skinned** by a non-elevated Azy (UIPI). It is
   detected and logged, but there is no fix short of asking for elevation, which
   the brief's "no admin" posture rejects.

### P3 — Polish and process

6. **No runtime screenshot exists anywhere in the project.** The images in
   `docs/images/` are offline previews. Every visual claim in the documentation
   is, strictly, a claim about geometry and colour maths — not about a screen.
7. **The installer has never been run end-to-end by a human.** CI compiles it and
   the script was reviewed line by line (install path, HKCU-only registry,
   task-dependent defaults, uninstall deleting Azy's own files and nothing else),
   but nobody has clicked Install → Run → Uninstall.
8. **The exe is unsigned**, so SmartScreen will warn. Not fixable without a
   certificate.
9. **The "Check visibility" diagnostic momentarily hides the skin** to compare
   two frames. It is user-triggered and restores itself on every path (audited),
   but it does briefly change what is on screen.
10. **Safe Mode can look like a regression.** After repeated failures the skin
    stays off until the user opts back in. It is logged and explained in the
    window; the wording is only as good as a log nobody has read yet.

---

## 5. Visual improvements

The audit made **no** free-standing visual changes. Redesigning the appearance
without being able to look at it would be guesswork, and the brief forbids random
visual changes. The three visual-adjacent changes above were corrections of
defects, not restyling:

- The debug overlay's text is now Segoe UI 12 DIP (ClearType, per DPI) instead of
  the stock GUI font at fixed 16 px spacing. At 150 % and 200 % the old text was
  blurred and the rows collided; the new rows are spaced in DIPs, so they scale
  with the display.
- The panel rectangles in the overlay now land where they claim to land (defects
  1 and 2). This changes what the user sees from the debug tool, and it is the
  whole point of fixing it.
- Log lines carry a bracketed level, which changes what a reader sees in the log,
  not in the window.

Direction for the work that follows — consistent with every standing constraint
(dark, glossy, glassy, premium, subtle; charcoal not black; 6/8/10 px corners;
1 px low-opacity borders; cheap shadows; no neon, no excessive gradients, no big
glows, no motion) — recorded here so it is not re-litigated:

- Regional treatment should use **one** faint 1 px hairline plus an even fainter
  2–3 px inner falloff at panel boundaries. Anything more reads as a drawn
  frame, which is exactly what a skinned application should not look like.
- The strongest available cue is **contrast at the edge**, not saturation in the
  middle: the 1 px lighter bezel is already doing the heavy lifting on a dark UI.
- The accent must stay an *extra* tint on the accent hairline; the base charcoal
  is deliberately blue-tinted already, and pushing the accent further turns
  "premium" into "RGB gaming".

---

## 6. Performance improvements

**Fixed**

- Removed the per-sync z-order walk (defect 3). This was the one real idle-cost
  defect: up to ~1000 `GetWindow` calls per second driven by unrelated events.
  After the fix, an idle Azy does nothing but one cheap comparison per second
  until an event arrives.
- Removed the stale panel-map rebuild that happened on every detach (defect 4);
  the map is now built once per geometry/workspace change.
- The debug overlay's font is created once per DPI instead of assumed, and
  released with the window; nothing is created per paint.

**Confirmed absent** (by repository-wide inspection, not by measurement)

- No animation loop, no worker thread (`CreateThread`/`std::thread` are absent
  from the whole of `src/`), and no polling loop. The one timer that exists is the
  1 Hz recovery net above; when the skin is suspended it is re-armed at 2 s, and
  when it fires nothing is redrawn unless the visual key changed (`SkinEngine::apply`
  compares a `VisualKey` before touching any surface, so an idle Azy does not
  repaint once per second).
- No screen capture (`BitBlt`, `PrintWindow`), no stretched-bitmap blit, no
  full-screen surface. The ring's bitmap is regenerated only when geometry, DPI or
  theme changes, then reused.
- The only pixel reads in the product are ~40 `GetPixel` probes inside the
  user-triggered visibility check (§4, item 9).
- No polling in the sense the brief forbids (nothing is re-read on a fast clock):
  process changes arrive over WMI events, window changes over `SetWinEventHook`,
  and the `DwmFlush` calls happen only during the diagnostic. Nothing enumerates,
  queries or draws on the 1 Hz safety-net tick unless an event has changed the
  answer.

**Not measurable here:** the 0–1 % idle / <2 % monitoring / <100 MB targets. The
code has no mechanism that could plausibly consume more (one 1 Hz timer that
usually does nothing, no capture, no thread, no per-pixel work, ~550 KB on disk
and no runtime dependency), but a
claim of "verified performance" would be false. See
[`VERIFICATION.md`](VERIFICATION.md) §6.

---

## 7. Compatibility

| Area | State | Notes |
| --- | --- | --- |
| Windows 10 1809 (17763)+, 64-bit | Target | Declared requirement; compiled for `x86_64-w64-mingw32`; `x64compatible` in the installer (ISPP-guarded so older Inno still compiles) |
| Windows 11 | Target | Dark window frame (immersive dark mode) requires build 22000+; below that the frame is skipped and the ring/veil still apply |
| Premiere Pro 2026 / 2025 / 2024 | Target | Detection is by executable name only (`adobe premiere pro.exe`, `- beta.exe`, `- headless.exe`); the skin is window-level, so the version does not change what is drawn. Version is read and logged |
| Other Adobe apps | Safe | Media Encoder, After Effects and Photoshop cannot match the allowlist, so they are never skinned |
| DPI 100 / 125 / 150 / 175 / 200 % | Modelled | Per-window DPI resolution, DIP-based maths, unit-tested at every step |
| Multi-monitor, mixed DPI | Modelled | Geometry follows the anchor window's own monitor; the primary monitor is never assumed |
| Elevation mismatch | Degraded | UIPI prevents drawing above an elevated Premiere; detected and warned |
| Full-screen exclusive apps | Not skinned | By design — Windows hides other windows |
| HDR / unusual colour setups | Untested | No HDR-aware blending is attempted |
| Languages / localisation | English only | No resources to translate; no locale-dependent parsing |

---

## 8. Runtime requirements

- Windows 10 build 17763 or newer, 64-bit, desktop session (WMI process events
  and `SetWinEventHook` are unavailable in a locked/no-desktop context — the
  visibility probe says so explicitly).
- No administrator rights. No `.NET`, no Visual C++ runtime, no installer
  framework: a single ~550 KB exe (560,128 bytes) with only OS DLLs imported.
- Premiere Pro 2024 / 2025 / 2026 (or Beta) installed and running. Azy can be
  started before or after Premiere, in either order.
- Azy and Premiere at the same integrity level for the skin to be visible.
- Settings and log live in `%LOCALAPPDATA%\Azy Skin\` and nothing is written
  anywhere else; the installer writes only its own program directory (under
  `%ProgramFiles%` by default) plus one HKCU `Run` value, and uninstalling
  removes Azy's own files, settings, log and startup entry only.
- The `setup.exe` is unsigned: SmartScreen will warn on first run.

---

## 9. Verification table

Condensed; the per-feature table with evidence is
[`VERIFICATION.md`](VERIFICATION.md).

| Area | Verified | Compiled only | Unverified |
| --- | --- | --- | --- |
| Build, versioning, release assets | ✅ `verify.sh` 1/2/4/5/6 (6/6 green), CI run `35521355164` for v1.2.2 | | |
| Pure logic (theme, DPI, panel map, settings, failure tracking, compat) | ✅ 578 checks, 0 failures | | |
| Detection, monitoring, tracking | | ✅ | |
| Ring / veil / DWM frame rendering | | ✅ | |
| Layering, click-through, focus safety | | ✅ | |
| Multi-monitor and DPI behaviour | maths ✅ | ✅ | |
| Appearance, screenshot, "does it look right" | | | ❌ **unverified — runtime screenshot unavailable** |
| Performance targets (CPU, RAM) | | | ❌ not measurable here |
| Installer install/uninstall | | ✅ script | ❌ never run by a human |
| Behaviour next to real Premiere Pro | | | ❌ never observed |

No screenshot is provided for the visual-regression section: **UNVERIFIED —
runtime screenshot unavailable.** Producing one requires a Windows machine with
Premiere Pro; `tools/preview_render.py` generates design previews, not
screenshots, and cannot substitute for one.

---

## 10. Final readiness

**READY FOR RUNTIME TESTING**

The code is clean, the forbidden techniques are absent, the tested logic passes,
and every defect that could be found without running the program has been found
and fixed. What stops this from being a higher state is not code quality — it is
that nothing here has ever been observed working on Windows next to Premiere Pro,
and the most recent observation the user gave (round 3: the skin was not in front
of Premiere) has never been re-tested. One run, with debug mode on, settles it.
