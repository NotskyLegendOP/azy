# Changelog

All notable changes to Azy Skin are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.3.0] — 2026-09-20

The architecture changed. Instead of decorating Premiere from the outside, Azy now
shows a **skinned copy of Premiere's own window** in a duplicate window placed
directly above it. Premiere stays the application in every sense - it keeps the
mouse, the keyboard, the focus, the menus, the timeline, the playback and every
plugin - and Azy decides what the pixels look like.

**Status: IMPLEMENTED - RUNTIME UNVERIFIED.** No part of the overlay has been run
against a real Premiere on a real machine yet. The design, the reasoning and the
proof of what could be checked are in `docs/AZY_OVERLAY_ARCHITECTURE.md`; the
checklist that closes the rest is `docs/AZY_OVERLAY_TEST_PLAN.md`.

### Added

* **GPU window capture.** Windows Graphics Capture, scoped to the single tracked
  Premiere window (never the desktop, never a monitor, never another application),
  with a free-threaded frame pool so no message pump is required. Cursor capture is
  switched off - the compositor draws the pointer, so mirroring it would show two.
  The owning process is validated before the capture starts, so a recycled window
  handle cannot make Azy mirror something else.
* **The duplicate window.** A click-through, never-activating, non-topmost window
  placed directly above Premiere, drawn with DirectComposition from a flip-model
  swap chain with premultiplied alpha. Click-through *across processes* is carried by
  `WS_EX_LAYERED | WS_EX_TRANSPARENT` — `HTTRANSPARENT` alone only forwards inside
  the same thread — and the window is created without a redirection bitmap, so there
  is no GDI surface that could ever be painted behind the mirror.
* **The composition shader.** One full-screen pass, embedded in the executable and
  compiled at start-up: charcoal darkening, a glass sheen, a lifted black level, an
  edge vignette, 1px panel separators, the 1px lighter bezel, rounded corners, and
  a soft accent - **with the Program and Source Monitors passed through untouched**,
  so footage is never darkened.
* **Smart pacing.** 10 frames per second while the window sits still, 30 while it is
  changing, driven by the capture itself (frames arriving means the window is doing
  something; 5/24 in performance mode). The frame pool is rebuilt *after* the frame
  in hand has been copied and released, so a resize can never invalidate a texture
  being read. The render timer is removed entirely while the duplicate is hidden.
* **Failure containment.** Every overlay failure ends with the ring and the veil
  still doing their job. An unsupported host is reported once and never retried;
  other failures are retried with backoff and then given up on for the session.
  Overlay problems deliberately do not feed Safe Mode.
* Overlay diagnostics: window handles and process ids, both rectangles, DPI,
  capture and present counters, pass-through regions, pacing and capture state, in
  debug mode and in the settings window's diagnostics.
* `tools/check-overlay.py`, run as step 3 of `scripts/verify.sh`: the shader's
  constant buffer and the C++ struct must agree member by member, the region slot
  counts must match, no `float3` may appear in the buffer, the C++ `static_assert`
  must equal the size the HLSL packing rules produce (272 bytes), nothing may call
  the monitor form of the capture API, the own-process guard must still exist, and
  the duplicate window must still be layered, transparent and non-activating, with
  no `*TOPMOST` anywhere and `HTTRANSPARENT` still answered. The checker was itself
  tested by breaking each of those on purpose and confirming it failed every time.
* `docs/AZY_OVERLAY_ARCHITECTURE.md` (11 sections, the feasibility decision, the
  error-recovery matrix) and `docs/AZY_OVERLAY_TEST_PLAN.md` (first-run checklist,
  targeted checks, ten scenarios).

### Changed

* The ring and the sheet stand down while the duplicate is on screen: they would be
  behind it, so drawing them would be paying twice for invisible pixels.
* The duplicate only appears once the capture has produced a frame, so no empty
  window is ever shown where Premiere's UI should be.
* The panel model now drives the shader (pass-through rectangles and separators)
  rather than only the debug view.
* Test suite grew to **649 checks** (from 578): overlay geometry mapping, panel
  clipping and packing, `overlay_rect`, and the style derivation from the theme.
* `verify.sh` has seven steps now (the overlay contract check is new).

### Known limitations of this release

* The window-style combination is the least-documented part of the feature; its
  failure mode is "nothing is shown" rather than an opaque box, and §4 of the
  architecture document names the knob.
* Widgets are still not restyled - the skin applies to pixels, and changing
  Premiere's controls would require injection, which stays permanently out of
  scope.
* The mirror is a frame or two behind, which shows while dragging or scrubbing.
* Panel separators come from a ratio layout model and can be a few pixels off on an
  unusual workspace.
* `d3dcompiler_47.dll` is required for the composition shader (present on every
  Windows 10 1809+ machine); without it the duplicate is not created and the static
  layers are used.
* Everything in `docs/AZY_OVERLAY_ARCHITECTURE.md` §11 is unverified.

## [1.2.3] — 2026-09-20

A deep second-pass review: architecture, runtime behaviour, Windows API usage and
resource lifetime. Seven defects in code were found and fixed, three of them in
paths that run on every event. The reports are
`docs/ARCHITECTURE_REVIEW.md`, `docs/DEEP_BUG_REPORT.md` and
`docs/RUNTIME_RISK.md`.

### Fixed

* **A blocking cross-process call on every window of the desktop.** The window
  enumerator fetched each window's title (a `WM_GETTEXT` send that blocks on the
  owning process' UI thread) and then discarded it - a leftover from a scoring
  heuristic that no longer exists. Any hung application on the machine could
  freeze Azy's message loop. The fetch is gone, and the enumerator now filters by
  process id first, so per-window work happens only for Premiere's own windows
  instead of for hundreds of unrelated ones.
* **The idle scan storm.** While nothing was tracked, every window event anywhere
  on the desktop scheduled a full process snapshot, at up to five per second. The
  detector now decides whether window activity is worth acting on (a target whose
  window has not appeared yet, or no target and no process observer), and the
  fallback cadence is 1 s instead of 0.2 s. With the observer live, an idle Azy
  scans nothing at all.
* **A GDI resource leak: one device context and one DIB section per resize.** The
  DIB stayed selected in the memory DC, so `DeleteDC` and `DeleteObject` both
  failed and both handles leaked on every resize, DPI change and monitor move -
  until the process ran out of GDI handles and the ring silently stopped being
  drawn. The previous object is now restored before either deletion.
* **A recycled window handle could be restyled.** Only `IsWindow` was checked, and
  Windows recycles handle values: a handle that named Premiere's frame a moment
  ago could name an unrelated window, which Azy would then give a dark title bar
  and a ring - or, on detach, "restore" attributes on. New
  `window_belongs_to(hwnd, pid)` is now checked in the tracker and in the DWM
  composer before anything is written or drawn.
* **A dead once-per-second desktop enumeration.** A diagnostics counter nothing
  read was computed by enumerating every top-level window on the desktop and
  asking DWM whether each visible one was cloaked. Field, code and helper
  deleted.
* **The DWM dark frame was not un-applied when the feature was switched off**
  (performance mode, Safe Mode, a per-feature override) while attached: the title
  bar stayed dark while every surface of Azy's own UI said otherwise. It is now
  restored immediately.
* **A dangling stack pointer in the version-string fallback:** the query buffer
  was declared inside the block that filled it and used after that block ended.

### Removed

* Dead code with no callers: `PremiereProbe::find_top_level_windows`,
  `win_util::window_text`, `SkinTarget::window_count`.

### Changed

* The veil caches its brush instead of creating one inside every paint.

## [1.2.2] — 2026-09-20

A complete implementation audit, and the defects it found. Nothing was added to
the feature set; seven defects in code were fixed, three documents were made
accurate, and two leftover scratch scripts were removed from `tools/`.

### Fixed

* **The debug overlay drew screen coordinates inside its own window.** Panel
  rectangles are in screen space, but the overlay's window sits at the frame
  origin, so every rectangle was displaced by the window position - on a maximised
  window by the caption height, and on a window that was not near the origin the
  whole overlay landed outside its own client area. It now records the frame
  origin when it is presented and converts at paint time.
* **The panel map was built from the window rectangle instead of the client
  area.** `GetWindowRect` includes the caption and the invisible resize border, so
  the menu-bar band was drawn across the caption and every region below it was
  shifted down by 30+ px (more at high DPI). A new
  `win_util::window_client_rect()` (documented, DPI-correct) now supplies the
  client area, and the frame origin and client origin are tracked separately.
* **The z-order was re-asserted on every sync.** Stacking can only change with the
  foreground, but the call ran on each window event - up to roughly a thousand
  `GetWindow` calls per second during editing, for a condition that could not have
  changed. It is now gated on `foreground_dirty || changed`.
* **A stale panel map survived detaching from a window.** It is now cleared on the
  suspend/remove path, so region work can never receive rectangles for a window
  that is gone.
* **The debug overlay used the stock GUI font at a fixed 16 px line spacing**,
  which is blurry and cramped at 150-200 %. It now creates a Segoe UI 12 DIP
  ClearType font for the window's DPI, spaces its rows in DIPs, and releases the
  font with the window.
* **The Animations switch did nothing.** It was stored, validated and shown, but
  no code path read it. The setting and its INI key are kept (an existing
  configuration is never rewritten) and the control is now disabled and labelled
  "not available - Azy is static", so the window states the truth instead of
  promising motion. See `docs/KNOWN_LIMITATIONS.md`.
* **Log level fields are bracketed and fixed width** (`[INFO ]`, `[WARN ]`,
  `[ERROR]`, `[DEBUG]`), so a log can be grepped for a level; the columns still
  line up.

### Changed

* Documentation accuracy: four files quoted "~440 KB" for an executable that is
  560,128 bytes, and `docs/PERFORMANCE.md` described the idle timer's work as
  "a few atomics". Both now say what is true, and the release-artifact inspection
  enforces a 1 MB size budget so the number cannot drift again.
* `tools/preview_render.py` states in its docstring that its palette is
  transcribed from `theme.cpp` by hand and that no check compares the two; the
  shipped images are welcome-page illustrations, not screenshots.
* Removed `tools/.tmp_ver.py` and `tools/.tmp_patch_settings.py`, one-shot scripts
  that should never have been committed.

### Added

* `docs/AZYSKIN_AUDIT.md`, `docs/VERIFICATION.md` and
  `docs/KNOWN_LIMITATIONS.md`: the audit, a per-feature statement of what is
  verified versus only compiled versus unverified, and the product's honest
  limits. The audit's readiness verdict is **READY FOR RUNTIME TESTING** -
  nothing in this build has been observed running on Windows next to Premiere Pro.

## [1.2.1] — 2026-09-20

Debug mode and the panel map: the first half of Phase 5, and the tool that makes
the second half exact instead of guessed.

### Added

* **Debug mode** (Settings → Advanced): draws the panel map Azy currently believes
  in straight over the tracked window - the menu bar and header bands, the left
  column (Project over Effect Controls), the two monitors, the right dock and the
  meter strip, the timeline - each labelled with its size, plus a block of facts:
  window handle and class, client rectangle, DPI and scaling, monitor rectangle and
  the workspace in use. It is click-through like every other Azy surface, so the
  window underneath stays fully usable while it is shown, and it costs nothing
  while it is off (no window is created at all).
* **The panel map** (`core/panel_map`): Premiere's docked panels are not windows,
  so "where is the timeline?" has no API answer. The map is the answer Azy uses -
  a workspace profile of *ratios* (plus DIP heights for the two fixed bands)
  applied to the client rectangle at the window's DPI. Eleven panels, six
  profiles. A rectangle too small to be a panel is reported as unusable rather
  than decorated, so a cramped floating window costs a missing region, never a
  broken layout.
* **UI profile** (Settings → Advanced, `ui_profile` in `settings.ini`): choose the
  layout the model assumes - Auto (Editing), Editing, Color, Audio, Effects or
  Graphics (spec §39's "manual UI profile", and §37's version → profile → rules
  chain with the table in configuration instead of code).

### Notes

* The map is rebuilt only when the geometry it was built from changes: one
  comparison per apply, one log block per real layout change, nothing on a timer.
* 211 new portable-core checks (578 total) cover panel-map adjacency, the fixed
  bands scaling with DPI, the exact agreement between `usable` and the minimum
  size, the degenerate cases (an empty client, a window smaller than one panel, a
  minimum larger than the window), workspace parsing and the INI round-trip of the
  two new keys.
* Debug mode is deliberately *not* gated behind "experimental features": it draws
  a diagnostic picture and changes nothing about the skin, and it is the tool a
  user needs exactly when something is wrong.

## [1.2.0] — 2026-09-20

The design system arrived: one accent, four presets, and two settings that were
missing from the configuration file.

### Added

* **An accent colour** (Settings → Appearance → *Accent*): blue-violet by default,
  with blue, violet and *Neutral* as the alternatives. It is used the way the
  reference uses it - as light, not as paint: a 1px lit edge, the window frame
  border, and about a tenth of the hue inside the overlay so the tint belongs to
  the same material as the edge. *Neutral* reproduces the previous look exactly
  (a white hairline and untouched charcoal), and the strength slider goes to zero,
  so the accent can be switched off as well as tuned.
* **Accent glow** (*Glow intensity*): raises the alpha of that lit edge. It never
  widens the band - at 100% it is still an edge rather than a halo. Off by default,
  because it is the one setting here that looks cheap when overdone.
* **The four quality presets** (spec §40): *Ultra*, *Balanced* (the recommended
  default and byte-identical to the 1.1.x look), *Performance* and *Low power*.
  One combo sets glass, border, radius, shadow, darkness, overlay strength, accent,
  glow, animations and Performance mode together. The combo reports *Custom* as
  soon as any single slider no longer matches, so it never claims a preset that is
  not in effect.
* **`animations`** (Settings → Appearance, off by default): a short fade for Azy's
  *own* layers. Premiere's interface cannot be animated from an outside process at
  all, so this can only ever affect Azy's ring and overlay - which is why the
  default is off and why the earlier "no animation" rule still holds.
* New `settings.ini` keys: `accent`, `accent_intensity`, `glow_intensity`,
  `animations`, `preset` (written for information; the individual keys always win).

### Fixed

* `theme.cpp` resolves the accent once, so the hairline, the frame border and the
  overlay tint cannot drift apart when one of them is changed.

### Notes

* No behaviour changes for anyone who does nothing: the defaults *are* Balanced.
* 61 new portable-core checks (367 total) cover the accent keys and hues, the
  promise that *Neutral* equals the pre-accent look, glow bounds, preset values and
  their distinctness, *Custom* detection and the new INI round-trip.

## [1.1.1] — 2026-09-20

The fix for "I don't see anything": the two ways Azy could be running perfectly while
showing nothing at all.

### Fixed

* **The tray menu never ran anything.** Every item - *Skin Enabled*, *Theme*,
  *Settings*, *Start with Windows*, *Suspend Skin*, *Reload Configuration*, *Open Log
  File*, *Exit* - was handed to the notification handler, which expects the shell's
  notification layout (and rejects anything that is not its own icon id or code), so
  each command decoded as "not for me" and returned. Nothing was logged, nothing
  happened. Menu commands now have their own id range and their own dispatch, and the
  menu works. (Until now, the reliable way to reach *Settings* was to start Azy a
  second time - a second launch opens the settings window, which is why the menu
  being dead was easy to miss.)
* **Azy's layers could be left behind the Premiere window.** Activating Premiere is
  enough: Windows raises the active window to the top of its band, and Azy's ring and
  overlay are ordinary windows - so they ended up *behind* the window they decorate,
  where they are invisible, while every single call still reported success. The skin
  now checks its own z-order against the tracked window and puts itself back in front
  (a walk of the z-order, then a `SetWindowPos` per layer only when the order is
  actually wrong), on every state change and on the low-frequency settle tick. The
  same check refuses to leave a surface behind an opaque window: if Windows will not
  let it stay in front, it is hidden and reported instead of pretending.
* **Installing a new build while Azy was running did nothing.** The new process saw
  the single-instance mutex, exited quietly, and left the old build in charge - the
  built-in cause of "I installed the update and nothing changed". A launch now asks
  the running instance which build it is: the same build just opens the settings
  window (unchanged), a *different* build is asked to release the skin and exit, and
  the new build takes over. If the old instance does not answer within four seconds,
  Azy says so instead of disappearing.
* The status word in the settings window and the tray tooltip no longer ignores the
  overlay: with the edge ring unavailable but the overlay on screen the state is
  `active`, not `partial`.

### Added

* **Tray menu → *Restart as Administrator***, shown only when it is the answer to a
  real problem: when Premiere Pro is running with administrator rights, Windows
  refuses to let a non-elevated process draw above it, and Azy cannot work around
  that. One UAC prompt later Azy runs at the same level and the skin appears.

### Notes

* No settings changed, and nothing new is written outside `%LOCALAPPDATA%\Azy Skin`.
* The first line of the settings window's diagnostics now starts with the version of
  the build that is running, so a report always says which build produced it.

## [1.1.0] — 2026-09-20

The skin now covers the whole window, not only its edge.

### Added

* **The whole-window overlay.** Azy's ring decorates the edge of Premiere's window;
  the overlay covers everything inside it, which is what makes the application read
  as *skinned* rather than outlined. It is one click-through, non-activating,
  layered top-level window the size of the tracked window, filled with a single
  translucent charcoal colour and blended by Windows with a constant alpha
  (`SetLayeredWindowAttributes`): no bitmap, no per-pixel work, no animation, and
  nothing new to monitor. It is on by default at 45% strength, sits directly above
  Premiere and directly *below* the ring (so the 1px hairline and the bezel stay
  crisp), and is hidden with the ring whenever the skin is suspended.
* Settings → Appearance gained two entries: **Cover the whole window (overlay)**
  (on/off) and **Overlay strength** (0–100%, default 45%). At 0% only the edge
  treatment remains; at 100% the tint reaches 60%, which is the "make everything
  dark" end of the range. Both are in `settings.ini` as `overlay` and
  `overlay_intensity`, and the strength slider is disabled while the overlay is off.
* With the overlay on, the edge falloff deepens with it (10 px with the overlay off,
  ~40 px at the default strength, ~72 px at 100%), because a 10px band *plus* a tint
  reads as "a border and a wash" while a wide soft falloff reads as one skin. The
  single strength slider drives both, so turning the overlay off restores exactly
  the previous edge treatment.
* Settings → **Check visibility**: measures the composed desktop - hides the ring
  and the overlay for one frame, samples the same pixels again, and reports what
  changed. This is the difference between "every API Azy calls returned success"
  and "the user can see it", and it is the first thing to run when the skin looks
  like it is doing nothing. The same line goes to the log.
* The overlay is verified exactly like the ring: if Windows will not let it be
  placed above Premiere's window (a higher-integrity Premiere), the attempt fails
  loudly - a warning in the log, a reason in the settings window - instead of
  silently covering nothing.
* The settings window now names the build it belongs to in the diagnostics block
  (`Azy Skin 1.1.0 - window 'Premiere Pro' …`), so a screenshot always says which
  version produced it.
* `tools/check-version.py` checks all five copies of the version number (CMake,
  the fallback header, the installer, the resource file, the release workflow) and
  runs as part of `scripts/verify.sh`.

### Fixed

* The overlay painted one pixel of itself. Its client area was only painted when
  something invalidated it, and the window class has no `CS_HREDRAW`/`CS_VREDRAW`
  and no background brush, so growing the window from its 1x1 creation size to the
  window it covers left the newly exposed area unpainted - a tint correctly
  configured, correctly positioned, correctly blended, and invisible. The repaint
  now happens explicitly, synchronously, once per colour or geometry change.
* A window that is not actually on screen (Premiere hides a few of its own
  top-level windows) is no longer decorated: it would have been painted off screen
  while every status line reported "active".
* A ring whose bitmap came out empty, or whose strips Windows would not place above
  the Premiere window, now reports a failure instead of a silent success.

### Notes

* The overlay is a tint, not a repaint: Premiere's own controls, panels and video
  previews are not modified in any way, they are simply composited under a
  translucent layer, exactly like any other window on the desktop.
* Because it covers the video monitors too, the strength is a trade-off: it is the
  first thing to turn down if you grade footage. The vignette (the wider edge
  falloff that comes with the overlay) is what frames the workspace without
  touching the picture.

## [1.0.2] — 2026-09-20

### Added

* **The settings window now answers "is the ring actually on screen?"** Two or
  three small diagnostic lines sit under the status:

  ```
  Window 'Premiere Pro' 1920x1040 at (0,0) | maximized | screen (0,0)-(1920,1080) | 100%
  Ring 12px at (0,0)-(1920,1040) | brightest pixel 199/255 | in front of Premiere: yes
  ```

  The first names the window Azy attached to and the geometry it works from (a
  wrong window or a surprising rectangle is visible at a glance), the second names
  the rectangle the ring was drawn on, the strongest pixel the renderer produced
  (`0` would mean an invisible bitmap) and whether the strips are in front of
  Premiere. When a line is missing, the panel says why instead: *"Ring: not on
  screen - …"*. A screenshot of that panel is now enough to diagnose a report.
* Azy notices when Premiere Pro runs with administrator rights and Azy does not,
  and says so in the log, the panel and a tray notification. Windows never lets a
  window of a lower integrity process be drawn above a higher integrity one, so
  the skin cannot be visible in that configuration - previously it simply looked
  like Azy was doing nothing.
* **Azy now speaks up when the skin cannot be seen.** If the ring is expected but
  the engine could not put it on screen (the strips were placed behind Premiere,
  the bitmap came out empty, the surface refused to present), that is logged once
  as a warning and shown as a tray notification with the reason, instead of leaving
  a silent utility that looks like it is doing nothing.
* One `window frame:` line per geometry change records what Windows reports for
  the tracked window (DWM bounds and `GetWindowRect`), the rectangle the ring is
  drawn on, the screen and its work area, the window state and the DPI - the
  complete reasoning behind the ring's placement in a single line.
* `tools/check-version.py` verifies that the version in `CMakeLists.txt`,
  `version_string.hpp`, `AzySkin.iss`, the resource file and the release workflow
  all agree, and `scripts/verify.sh` runs it on every push.

### Fixed

* The ring frame is now clipped to the display as well. A borderless window that
  Windows does not report as maximized (Premiere's own fullscreen mode, for
  example) hangs over the monitor edges just like a maximized one, and was
  decorated off screen for the same reason.
* **A hidden window is no longer decorated.** Premiere keeps a few hidden
  top-level windows; if one of them was ever tracked, Azy drew a ring around
  nothing while every status line said "active". A window has to be on screen to
  be skinned, and the status now says "Premiere window hidden" when it is not.
* **A ring that renders nothing is reported as a failure** instead of a success:
  the strongest alpha in the painted bitmap is checked before the surface is
  declared visible, so an empty ring can no longer pass as a working one.
* The strips are verified against their requested rectangles after placement, and
  a strip Windows moved elsewhere is logged (and shown in the panel) rather than
  silently producing a misplaced ring.

## [1.0.1] — 2026-09-20

### Fixed

* **A maximized Premiere window could be decorated off screen.** Windows places a
  maximized window so that its invisible resize border hangs over the monitor
  edges, and the DWM extended frame bounds can report exactly those coordinates.
  Azy draws the ring just inside the frame edge, so on such a window the whole
  treatment could land outside the visible desktop: every call succeeded, the log
  stayed quiet and the settings window said "active" while nothing was on screen.
  The ring frame is now derived from what the user can actually see - the monitor
  work area for a maximized window, the whole monitor for a fullscreen one, and the
  reported bounds for a windowed one (`ring_frame()` in
  `include/azy/core/ring_layout.hpp`, covered by unit tests).
* The settings window headline now reports what the skin engine really did
  ("active", "partial", "idle", or the reason it is suspended) instead of what the
  configuration would allow. Saying "active" while nothing is on screen is the one
  thing that window must never do.

### Added

* One info-level line whenever the ring changes:
  `ring: 1920x1040 frame at (0,0), 12px thick, band 10px, radius 8px, 288 KB,
  strongest pixel alpha 199, above Premiere: yes`. It records where the ring was
  drawn, the strongest pixel the renderer produced (0 would mean an invisible ring)
  and whether the strips are in front of the Premiere window - so "the skin does
  nothing" can be diagnosed from the log instead of guessed at.
* The version in the runtime banner now comes from `project(... VERSION ...)` in
  `CMakeLists.txt`, so the banner, the installer and the executable's version
  resource cannot drift apart.
* A one-time log line when a maximized window's reported frame hangs over the
  display, naming both rectangles.

## [1.0.0] — 2026-09-20

First release. Azy Skin 1.0.0 is a native Win32 visual skin for Adobe Premiere
Pro: it makes the application frame darker, cleaner and slightly glassy without
changing anything about how Premiere works.

### Added

**Detection and lifecycle**
* Premiere Pro detection by process *name* (never a hardcoded install path),
  covering `Adobe Premiere Pro.exe` and the Beta executable; the headless
  encoder host is recognised and deliberately ignored.
* Version identification from the executable's version resource, mapped to a
  year-aligned release family (2022 … 2026, plus `legacy`, `next` and `unknown`).
* Instant start/stop notification through WMI process events, with graceful
  fallback to snapshot-on-event when WMI is unavailable.
* Automatic attach on launch, re-targeting when a different build starts, and
  complete resource release when Premiere exits or crashes.

**Skin engine**
* Level 1: dark title bar, charcoaled caption/border/text colours and rounded
  window frames through documented DWM window attributes — each read before it is
  written, and restored on OFF, on suspend, on Premiere exit and on Azy exit.
* Level 2: a click-through, non-activating ring
  (`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`) built from four thin
  layered strips (top/bottom/left/right). It carries a 1px hairline border, a 1px
  raised bezel that keeps the edge legible on a dark UI, a soft quadratic inner
  shadow, a barely visible top highlight and an optional translucent wash — all
  painted once per change with GDI+ in frame coordinates and presented with
  `UpdateLayeredWindow`, so a 1px line is exactly one pixel and the ring costs
  ~280 KB of bitmaps on a 1080p window rather than a 33 MB window-sized ARGB layer.
  A *lighter* 1px bezel sits just inside the hairline, because a purely dark edge
  treatment is invisible over Premiere's own near-black panels.
* Optional (Advanced, off by default) Windows 11 Mica backdrop on the window
  frame.
* `Original` theme that removes every Azy change; `Azy Dark` for an opaque
  treatment; `Azy Dark Glass` as the default.

**Performance**
* Fully event-driven: `SetWinEventHook`, WMI process notifications, directory
  change notifications and Windows broadcasts. No polling loop, no animation, no
  screen capture, no continuous rendering.
* A visual-key comparison that makes a no-op apply cost nothing: a static
  Premiere window produces no DWM calls and no repaints.
* Suspension for minimized, inactive (opt-in), cloaked and dragging states.
* Performance mode: static colours, no shadow, no translucency, minimum cadence.
* ~440 KB executable, no third-party dependencies, no UI framework, no GPU work.

**Compatibility and safety**
* Host capabilities probed at runtime (each DWM attribute is attempted and the
  result is recorded) rather than inferred from a Windows build number.
* Version-aware treatment with conservative fallbacks for unknown, future, legacy
  and Beta builds.
* Per-monitor-v2 DPI awareness; 1px stays exactly 1px at 100/125/150/175/200%;
  geometry taken from `DWMWA_EXTENDED_FRAME_BOUNDS` so the skin matches the
  visible frame rather than the invisible resize border.
* Multi-monitor support with no assumption about the primary monitor.
* Failure tracking (3 failures in 5 minutes) with persistent **Safe Mode**, a
  plain-language explanation and a manual way back.
* `Azy Skin never becomes a dependency`: it holds no handles into Premiere,
  installs nothing into it, and can be closed or killed at any time.

**User interface**
* System tray: status line, Skin Enabled, Theme submenu, Settings, Start with
  Windows, Suspend Skin, Reload Configuration, Open Log File, Exit. Single left
  click toggles the skin; Explorer restarts are handled.
* Settings window (pure Win32, dark, no framework): Skin, Appearance,
  Performance and Advanced groups; every change applies live; DPI-change
  re-layout.
* Hand-editable `settings.ini` with automatic reload, warning on invalid values,
  and round-tripping of unknown keys.
* Quiet logging with repeat collapsing and one-time rotation at 512 KB.

**Packaging**
* Inno Setup 6 installer: per-user, own directory, no Adobe content touched,
  installer choices handed to the application as a one-shot file, removal of
  Azy's files, settings, log and startup entry on uninstall.
* Portable core unit tests (220+ assertions) runnable on any platform, plus CI
  jobs for native tests, a full Windows cross-compile gate, the MSVC build and
  the installer.

### Known limitations

* Windows 10 cannot recolour or round a foreign window's frame — only the dark
  title bar attribute exists there, so the treatment is "dark frame + ring".
* Premiere's internal panels, tabs, buttons, menus and dialogs are drawn by
  Premiere itself and are intentionally not touched. See
  [`docs/LIMITATIONS.md`](docs/LIMITATIONS.md).
* Floating (undocked) Premiere panels are not decorated.
