# Azy Skin — Compatibility

Two things decide what Azy Skin is allowed to do:

1. **What the host Windows build can do** — probed at runtime, never guessed from
   a version number.
2. **Which Premiere Pro build is running** — read from the executable's version
   resource, then mapped to a compatibility family.

The combination is resolved by `resolve_features()` in `include/azy/core/compat.hpp`
and is fully unit tested (`tests/core_tests.cpp`, "version-aware compatibility
policy").

---

## Host: Windows

Minimum supported host: **Windows 10, version 1809 (build 17763)**. That is where
the oldest technique Azy depends on (the dark title bar attribute) appears.

Capabilities are **probed**, not assumed. At first use, Azy creates a throwaway
window and sends each documented DWM attribute to DWM, checking whether DWM
accepts it. This is more reliable than build-number sniffing and keeps working on
future Windows releases.

| Capability | Attribute | Available since |
|---|---|---|
| Dark title bar | `DWMWA_USE_IMMERSIVE_DARK_MODE` (20, or 19 on 1809–20H1) | Windows 10 1809 |
| Window frame colours | `DWMWA_CAPTION_COLOR`, `DWMWA_BORDER_COLOR`, `DWMWA_TEXT_COLOR` | Windows 11 22000 |
| Rounded window frames | `DWMWA_WINDOW_CORNER_PREFERENCE` | Windows 11 22000 |
| System backdrop (Mica) | `DWMWA_SYSTEMBACKDROP_TYPE` | Windows 11 22621 |
| Own surface (ring) | `UpdateLayeredWindow` + GDI+ | Windows XP → every supported host |
| Event-driven observation | `SetWinEventHook`, WMI | every supported host |

Resulting treatment:

| Host | Treatment |
|---|---|
| Windows 11 (fully supported) | Dark + charcoaled frame, rounded corners, hairline ring, soft shadow, glass surfaces |
| Windows 10 1809+ | Dark frame, hairline ring, soft shadow, glass surfaces (no frame recolouring or rounding — Windows 10 cannot do them for a foreign window) |
| Windows 8.1 or older | Not supported. The application refuses to install (`MinVersion=10.0.17763`) |

Capabilities are re-resolved on `WM_SETTINGCHANGE`, so switching Windows'
light/dark mode, changing the accent colour or changing DPI policy is picked up
without a restart.

---

## Premiere Pro

Azy matches running processes on the **executable name** —
`Adobe Premiere Pro.exe`, `Adobe Premiere Pro Beta.exe` — and never on an install
path, so custom install locations, renamed drives, portable-ish setups and
multi-version installs all work. `Adobe Premiere Pro Headless.exe` (Adobe's
encoder/watchdog host) is recognised and deliberately ignored: it has no user
interface to skin.

The version comes from the executable's version resource
(`VS_FIXEDFILEINFO` → `FileVersion`, e.g. `25.6.0.58`), which is read-only, needs
no Adobe code and works for Beta builds too. The major number maps to Adobe's
year-aligned release:

| Major | Release | Family | Treatment |
|---|---|---|---|
| ≤ 21 | CC 2019 – 2021 | `legacy` | Conservative: dark frame + hairline ring; no frame colours, no rounding, no backdrop |
| 22 | 2022 | `2022` | Dark frame, frame colours, ring; no rounding |
| 23 | 2023 | `2023` | as 2022 |
| **24** | **2024** | `2024` | **Full treatment** |
| **25** | **2025** | `2025` | **Full treatment** |
| **26** | **2026** | `2026` | **Full treatment** |
| 27+ | future | `next` | Conservative-plus: dark frame + ring, no frame recolouring or rounding (a future release may restructure its frame) |
| unreadable | — | `unknown` | Conservative: dark frame + ring only, experimental features forced off |
| any + `Beta` in the executable name | — | `*`-beta | Full treatment minus rounding |

The policy is a table, not engine code: supporting Premiere 2027 is a change to
`family_from_major()` and a new row here.

### Why version matters at all

Premiere draws essentially its whole interface itself, so Azy's *own* surface
(section 2.1 of [`TECHNIQUES.md`](TECHNIQUES.md)) is version-independent: it draws
a ring inside the window frame and does not care what is inside. Version
sensitivity applies to the DWM frame attributes, because a future Premiere could
change its window structure (owner windows, frame extension into the client area,
custom chrome). Rather than assume, Azy degrades: unknown and future builds get
the treatment that cannot be wrong — a dark title bar and a hairline ring.

---

## Multiple Premiere windows and multiple versions

* The **main editor window** is chosen as the largest visible, non-cloaked,
  non-minimised, framed top-level window of the Premiere process. Splash screens,
  progress windows and tiny helper windows are rejected by size and style, and the
  selection is re-evaluated whenever the window set changes.
* If a second Premiere instance or a Beta build starts, Azy switches to it,
  restoring the previous window's frame first — **only one window is ever
  modified**.
* Floating panels and dialogs inside Premiere are not touched (see
  [`LIMITATIONS.md`](LIMITATIONS.md)).
* If Premiere is closed while a second instance is still running, Azy reverts and
  waits for the next event, then re-targets the remaining instance on its next
  window activity.

---

## DPI and multi-monitor

* Azy is **per-monitor-v2 DPI aware** — declared in the application manifest and
  reinforced at runtime with `SetProcessDpiAwarenessContext` (with
  `SetProcessDpiAwareness` as the Windows 8.1 fallback).
* Every coordinate Azy handles is a **physical pixel**. Logical sizes ("8px
  radius", "10 DIP band") are converted with the DPI of the monitor Premiere's
  window is *actually on*, obtained from `GetDpiForWindow` (falling back to
  `GetDpiForMonitor`, then the system DPI).
* A 1px hairline is exactly 1 physical pixel at 100%, 125%, 150%, 175% and 200% —
  `dip_to_px(1, 120) == 1` is a unit test.
* Moving Premiere to a monitor with different scaling produces
  `WM_DPICHANGED`/`WM_DPICHANGED_BEFOREPARENT`; Azy re-reads the DPI and re-applies
  at the new scale, so the ring never ends up blurry or misaligned.
* The ring is positioned from `DWMWA_EXTENDED_FRAME_BOUNDS`, not `GetWindowRect`,
  so it sits exactly on the visible frame edge regardless of scaling.
* The primary monitor is never assumed: monitor and work area come from
  `MonitorFromWindow` for the window in question.
* Monitor layout changes (`WM_DISPLAYCHANGE`) and device changes
  (`WM_DEVICECHANGE`) trigger a re-resolve, and resuming from sleep
  (`WM_POWERBROADCAST`) triggers a full Premiere re-check.

### Window states

| State | Behaviour |
|---|---|
| Windowed | Full treatment; ring follows the frame |
| Maximized | Full treatment; corner rounding skipped (as Windows itself does) |
| Fullscreen (borderless, frame == monitor) | Ring sits on the screen edge; rounding skipped |
| Minimized | Surface hidden, DWM work suspended, ~0 work until restore |
| Hidden / cloaked (virtual desktop) | Surface hidden, work suspended |
| Being dragged / resized | `EVENT_SYSTEM_MOVESIZE_START` hides the surface; on `MOVESIZE_END` (+120 ms settle, or a 1 s watchdog if the end event is lost) it reappears at the new geometry |
| Moved between monitors at different DPI | Re-applied at the new scale, `WM_DPICHANGED` handled explicitly |
| Premiere exited | Surface destroyed, frame restored, observers reset to "waiting" |
| Premiere crashed | Identical to the above — Azy holds no handles that require Premiere to cooperate |

---

## Known limitations by design

* Windows 10 cannot recolour or round a *foreign* window's frame; only the dark
  title bar attribute exists there. On Windows 10 the visual result is therefore
  "dark frame + Azy's ring", which is still the intended look, just less of it.
* Premiere's internal panels, tabs, scroll bars and buttons are drawn by Premiere
  and cannot be restyled by an external process. Azy does not pretend otherwise.
* Corner rounding applies to the window frame; on Windows 10 the frame stays
  square (see [`LIMITATIONS.md`](LIMITATIONS.md) for the reasoning).
