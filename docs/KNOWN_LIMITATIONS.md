# Known limitations

Everything Azy Skin cannot do, does not do yet, or has not been proven to do.
Nothing in this file is a bug report — the bugs found by the audit are in
[`AZYSKIN_AUDIT.md`](AZYSKIN_AUDIT.md). This is the honest boundary of the
product as shipped in **v1.3.0**.

Legend: **Permanent** = a consequence of the "no injection, no Adobe APIs, no
project changes" rule. **Open** = planned or possible later. **Unverified** =
implemented, but not proven on a machine that has Premiere Pro.

---

## 1. Permanent — outside the window, nothing can be reached

Azy is a separate process that owns its own windows. It can style the window
*around* Premiere and draw its own click-through layers on top of it. It cannot
reach inside.

| Not possible | Why |
| --- | --- |
| Theming Premiere's own widgets (buttons, sliders, tabs, checkboxes, dropdowns, tooltips, text fields) | Requires UXP / CEP / a plug-in / code injected into Premiere's process. All forbidden. |
| Relighting a specific panel's contents (darken the Project panel's list, restyle the timeline's tracks) | Same: only pixel-level and window-level access exist from outside. |
| Animating anything Premiere draws | Same, plus it would need a repaint loop that the brief forbids. |
| Reading the current workspace or panel layout | That state lives in Premiere's process and its preferences. Azy models the layout from published geometry instead (see §3). |
| Changing Premiere's own preferences, project, sequence, or any file Adobe owns | Forbidden, and unnecessary for a skin. |
| Applying the skin while Premiere runs elevated and Azy does not | Windows UIPI: a lower-integrity process cannot draw above a higher-integrity one. Azy logs a warning and Azy's ring is composed *behind* Premiere. Run both at the same level. |

Region treatment is therefore **closest-possible**, not exact: Azy models where
Premiere's major panels are and draws over those areas. Premiere's pixels are
never modified — Azy only decides what to put on top of them.

## 2. Installation and distribution

- **The executable is not code-signed.** A certificate is not available for this
  project. Windows SmartScreen will show "Windows protected your PC" on first
  run of a downloaded copy; **More info → Run anyway** is expected. The
  portable `.zip` avoids the installer, not SmartScreen.
- **The `setup.exe` is built only by CI.** Inno Setup is a Windows tool, so a
  local build of the installer is not possible in this environment; the release
  asset is produced by the GitHub Actions job from
  `packaging/AzySkin.iss`. The result of that job is *compiled and installed*
  successfully in CI, but the install → uninstall round trip has never been
  performed by a human on a real machine.
- **No auto-update.** Azy never contacts the network. New versions are manual
  downloads. This is a deliberate choice, not an omission: the brief asked for a
  lightweight, offline tool.
- **x64 only.** There is no 32-bit or ARM64 build, and Premiere is 64-bit on
  Windows, so no 32-bit build is useful.

## 3. The panel map is a model, not a measurement

`src/core/panel_map.cpp` derives panel rectangles from ratios of Premiere's
client area plus fixed DIP bands, using each workspace profile
(Editing / Color / Audio / Effects / Graphics). It is used by the debug overlay
and is the coordinate system all regional treatment will use.

- The ratios are the published layout of Premiere's default workspaces. A user's
  **customised** workspace (panels dragged, docks moved, a panel closed) will not
  match, and Azy has no way to know that — it cannot read Premiere's state. The
  manual **UI profile** combo in Settings ("UI profile") is the workaround: pick
  the workspace you are actually using.
- Panel rectangles are therefore **approximations**. They are exact about the
  frame (edge treatment, falloff, radius, veil) and approximate about the
  interior divisions.
- Panels smaller than 24 px in either direction are skipped rather than drawn
  degenerately.
- **No regional treatment is shipped yet.** P5 items "dock separators",
  "application header band", "timeline band", "monitor / project / effect
  controls bands" and "audio meter region" are still open; today the map only
  drives the debug overlay. The skin itself is the ring, the veil and the DWM
  frame.

## 4. Animation

`appearance.animations` is stored, validated, round-tripped through the INI, and
shown in Settings — but **no code path reads it**. Nothing in Azy animates:
Azy's layers are drawn once per geometry change and are static. The checkbox is
created disabled and labelled "not available — Azy is static" so the UI does not
promise motion it will not deliver. The key is kept so that existing
configuration files are never rewritten or rejected.

## 4b. The duplicate window — what it does not do

| Not done | Why |
| --- | --- |
| Restyling Premiere's widgets through the mirror | The mirror is pixels. Geometry inside the window is Premiere's. |
| Keeping perfectly in step during fast motion | One capture frame plus one present is spent before the duplicate shows anything, so a drag or a scrub is a frame or two behind. Static UI is unaffected. |
| Promising that the capture's pixel size equals the window's rectangle | Nothing in the API documents it. When they differ the mismatch is reported instead of being hidden (debug row `size agrees`). |
| Showing anything before the first frame arrives | The duplicate appears only once it has content, so the ring and the sheet stay visible for the first moment instead of flashing an empty window. |
| Working without `d3dcompiler_47.dll` | The composition shader is compiled at start-up from the embedded source. Every Windows 10 1809+ machine ships the DLL; where it is missing, the duplicate is not created and the static layers are used. |
| Working on a Windows build without the free-threaded capture frame pool | Reported once as unsupported; the static layers carry the skin. No retry storm, no Safe Mode. |
| Guaranteeing the 1px hairlines land on Premiere's real boundaries | The panel model is a ratio layout (see §3). A wrong hairline is the worst case, never a broken window. |
| Removing the Windows capture border where the OS refuses | Where the OS allows it, it is switched off; where it does not, Azy's window sits over it. |
| Finishing the mirror when Premiere runs elevated and Azy does not | UIPI, unchanged from earlier versions: place above an elevated window is refused, Azy reports `partial`, and the tray offers a restart as administrator. |

## 5. Runtime behaviour has never been observed

The development environment for this project is Linux. Every line of Win32,
GDI+, DWM and layered-window code in this repository has been **cross-compiled
and inspected, not executed**. That means:

- Anything that depends on Windows responding correctly is **UNVERIFIED**: layering,
  z-order, per-monitor DPI, multi-monitor placement, minimize/restore, DWM
  composition, the veil's alpha, tray behaviour, the settings window at 150–200 %
  scaling, and every part of the "does it look right" question.
- The pure logic (version handling, theme maths, panel map, DPI geometry,
  settings round-trip, failure tracking) is covered by 578 native checks that do
  run. See [`VERIFICATION.md`](VERIFICATION.md) for the exact split.
- Azy has never been run next to Adobe Premiere Pro. Behaviour alongside real
  Premiere — including the maximised-window case the user reported — is
  therefore **UNVERIFIED**.

No screenshot in this repository was taken from a live Premiere Pro window. The
images in `docs/images/` are generated by `tools/preview_render.py`, a design
preview that recomputes the ring geometry and palette offline.

## 6. Behaviour that is intentionally conservative

- **Safe Mode.** After repeated failures within the failure window, Azy disables
  the skin and stays off until the user re-enables the experimental path in
  Settings. This can look like "the skin stopped working" — it is the designed
  response, and the reason is written to the log and shown in the window.
- **Suspend options.** "Suspend when minimized" is **on** by default (a
  minimised window cannot show a skin); "Suspend when inactive" is **off** by
  default, so an unfocused Premiere keeps its skin. When idle Azy still tracks
  Premiere's window position (event-driven, not polled) so the skin follows
  immediately instead of reappearing late.
- **Floating (undocked) panels and dialogs** are not skinned yet (P6). Dialogs
  get the dark title bar where Windows supports it; undocked panels get nothing
  beyond the main ring.
- **Full-screen exclusive** applications cannot be overlaid by design
  (Windows hides other windows). Premiere in a normal maximised window is
  unaffected; this only matters for other software.
- **HDR and unusual display configurations** are untested. Azy works in the
  composited desktop's colour space and does not attempt HDR-aware blending.

## 7. Logging and diagnostics

- The log is a plain text file with rotation; it is not structured, not
  timestamped in UTC, and not sent anywhere.
- There is no `--diagnose` mode. The diagnostic path is the **Check visibility**
  button in Settings, which writes a report into the Settings window and the log:
  what Azy can see (the tracked window, its rectangle, Azy's own layered windows,
  composition and DPI facts) and the first reason it gave up if the skin is not
  showing. It cannot report anything about Premiere's internals, because Azy
  cannot read them.
- Command line switches are only `--tray`, `--settings`, `--reset`, `--debug`
  and `--no-tray`; there is no headless or scriptable interface.
