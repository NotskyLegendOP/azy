# Changelog

All notable changes to Azy Skin are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

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
