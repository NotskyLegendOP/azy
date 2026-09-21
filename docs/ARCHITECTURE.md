# Azy Skin — Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ app/          AppController · SettingsStore                                 │
│               the only place that knows about everything else; owns the      │
│               hidden message window, the tray, the settings window and all    │
│               policy decisions                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│ win32/        capture/      WindowCapture (GPU capture of one window) ·       │
│                             D3dShared (the device) · wgc_abi (WinRT ABI,      │
│                             declared by hand - the toolchain has no WinRT SDK) │
│               mirror/       MirrorRenderer (the duplicate window: swap chain,  │
│                             DirectComposition, mirror.hlsl, pacing)           │
│               detect/       PremiereDetector · PremiereProbe · ProcessScanner│
│               watch/        EventWatch (global WinEvent observer)            │
│               performance/  PerformanceManager (suspend policy + cadence)     │
│               skin/         WindowTracker · SkinEngine · InputGuard ·         │
│                                                                               │
│               os/           win_api (optional APIs) · win_util · win_version │
│                             autostart · file_watcher                          │
│               ui/           TrayIcon · SettingsWindow · app_icon              │
├──────────────────────────────────────────────────────────────────────────────┤
│ core/         version · product · theme · theme_tokens · settings · geometry · │
│               failure_tracker · log · strings · panel_map · capture_math ·    │
│               mirror_style (the shader's constants, derived from the theme)   │
│               portable C++17: no Windows headers, unit tested on any host     │
└──────────────────────────────────────────────────────────────────────────────┘
```

The `core/` layer is deliberately free of Windows dependencies, which is what
allows the version parsing, the theme engine, the mirror style derivation, the
panel model, DPI maths, the settings INI and Safe-Mode logic to be covered by
ordinary unit tests (`tests/core_tests.cpp`, 747 assertions) on any machine.

Everything Azy knows about Premiere arrives through `detect/`:

```
PremiereDetector ── detects/identifies──► ProcessRecord { pid, exe path, version resource, ProductInfo }
                                            │
WindowTracker ──── geometry/state/DPI ──────┼──► SkinTarget
                                            │
PerformanceManager ── suspend decision ─────┼──► SuspendReason
                                            │
SkinEngine ─── consumes ────────────────────┴──► the mirror window's geometry
                                                 + the theme's style constants
                                                 (see AZY_MIRROR_ARCHITECTURE.md)
```

## Data flow

```
 SetupProcessDpiAwarenessContext(PMv2)
            │
            ├─ Logger (file + OutputDebugString)
            ├─ Single-instance mutex ──► second launch: post "open settings" to the first
            └─ AppController::initialize
                   ├─ SettingsStore::load            (%LOCALAPPDATA%\Azy Skin\settings.ini)
                   ├─ EventWatch::start              (3 × SetWinEventHook, WINEVENT_OUTOFCONTEXT)
                   ├─ PremiereDetector::start        (optional WMI process notifications + snapshot)
                   ├─ TrayIcon::create               (NIM_ADD + NIM_SETVERSION(4))
                   ├─ SettingsWindow::create         (created lazily, hidden)
                   ├─ DirectoryWatcher::start        (settings.ini change notification)
                   └─ sync("startup")                ──► see below
```

`sync(reason)` is the single funnel the whole application goes through. It is
called from the message loop only — never from a callback — and it is where the
"is anything different?" decisions are made:

```
sync()
 1. EventWatch::consume_dirty()  ──► PremiereDetector::note_window_activity()
    PremiereDetector::pump()     ──► WMI events drained; rescan only if something possibly changed
 2. consume_location/foreground_dirty() ──► WindowTracker::refresh()  (geometry, DPI, monitor, state)
 3. PerformanceManager::evaluate() ──► suspend? which reason? which timer cadence?
 4. SkinEngine::apply()          ──► compares the desired "visual key" with what is on screen and
                                     touches the mirror window *only* when it differs
 5. arm_timer(cadence); update tray tooltip/settings status only if the line changed
```

Step 4 is the performance-critical one: `SkinEngine::apply()` builds a
`VisualKey` (window handle, overlay rectangle, DPI, visibility, a style revision
counter and the theme id) and compares it with the key it applied last time.
Identical request → immediate return: no window move, no constant buffer rebuild,
no present. The style revision only advances when a *setting* changed, so moving
the slider is the only thing that re-derives the whole style.

## Threading model

Azy is single-threaded by design, and that is a performance decision rather than
a simplification:

| Work | Runs on | Why |
|---|---|---|
| Message loop, mirror window, swap chain present, tray, settings window | the UI thread | window and shell calls are cheapest there and need no synchronisation; the GPU work is handed to the driver and does not block the message loop |
| WinEvent callbacks | the thread that registered the hook (UI thread) via `WINEVENT_OUTOFCONTEXT` | the handler only sets atomic flags and posts one message |
| WMI process notifications | the same STA thread, during message pumping | COM is initialised `COINIT_APARTMENTTHREADED`; callbacks arrive inside `GetMessage` and queue into a mutex-protected vector |
| `settings.ini` change detection | a thread-pool thread owned by Windows (`RegisterWaitForSingleObject`) | the callback does nothing but `PostMessage` |
| Process/window enumeration | the UI thread, only on state change | one toolhelp snapshot ≈ 1 ms and is needed only when something changed |

There is no worker pool and no render thread. The mirror's timer is the only
periodic work, its period comes from the pacing policy (60/15 fps, 30/8 in
performance mode) and it does not exist at all while the mirror is hidden — see
[`AZY_MIRROR_ARCHITECTURE.md`](AZY_MIRROR_ARCHITECTURE.md) §7 and
[`PERFORMANCE.md`](PERFORMANCE.md).

## Windows Azy owns

Exactly two, both trivial:

1. **Message window** (`AzySkin.MessageWindow`) — a zero-sized, never-shown
   `WS_POPUP` window with `WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE`. It receives the
   timer tick, the tray callback, the `settings.ini` notification, second-instance
   requests, and the Windows broadcasts Azy cares about (`WM_SETTINGCHANGE`,
   `WM_DISPLAYCHANGE`, `WM_POWERBROADCAST`, `WM_ENDSESSION`, `TaskbarCreated`).
   It is a real top-level window rather than `HWND_MESSAGE` precisely because
   message-only windows do not receive broadcast messages.

2. **The mirror window** — one `WS_POPUP` window with
   `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
   WS_EX_NOREDIRECTIONBITMAP`, placed on Premiere's visible frame, inserted
   directly above Premiere (never topmost), and painted entirely by
   DirectComposition from a GPU swap chain. It has no input path at all, and it is
   destroyed with every GPU resource it owns when the skin is suspended or Premiere
   closes. It is the only visual element Azy creates — the ring, the sheet and the
   DWM frame attributes of v1.0–v1.3 are gone (see
   [`AZY_MIRROR_ARCHITECTURE.md`](AZY_MIRROR_ARCHITECTURE.md)).

The settings window is the only other window Azy creates, and only when the user
asks for it. Debug mode (spec §38) is not a window: every fact it asks for lives in
the settings window and in one grouped log line per change.

### Which layer is in charge

There is exactly one path now. If the settings say the skin is visible and
Premiere's main window is validated, the mirror is placed and fed by the capture
session. If the capture cannot run — unsupported Windows build, a driver that
refuses the device, an elevated Premiere — Azy draws **nothing** rather than
falling back to a lesser skin: a rectangle painted over Premiere with no live
picture is exactly the "fake working state" the rebuild removed. The state machine
names that state and the diagnostics say why.

## Premiere event lifecycle

```
                  ┌─────────────────────────────────────────────────────────┐
 WMI: process     │ EventCreated/Deleted(Win32_Process)                     │
 creation event   │   ├─ name matches a Premiere executable?                │
                  │   │    ├─ yes → rescan → read identity → publish Started│
                  │   │    └─ no  → ignored                                 │
                  └─────────────────────────────────────────────────────────┘

 Started(pid, window?)  →  WindowTracker::set_window() + EventWatch watched pid/thread
                        →  sync()  →  SkinEngine::apply()  →  mirror placed and fed

 WinEvent: EVENT_OBJECT_LOCATIONCHANGE / MOVESIZE / MINIMIZE / FOREGROUND / CLOAKED
                        →  flags  →  sync()  →  WindowTracker::refresh()
                        →  PerformanceManager decision  →  apply / hide / suspend

 WMI: process deletion  →  EventKind::Stopped → skin removed, resources released
```

## Premiere version compatibility

There is no capability policy to resolve any more. The v1.0–v1.3 releases had one
because they styled Premiere's *frame* with DWM attributes, and which of those a
given Windows build accepts decided what the skin could do. The mirror does not
touch Premiere's frame: it captures the window and draws its own, so the only
compatibility questions left are answered by trying:

* **Can this Windows build capture a window?** `WindowCapture::start` returns
  `Unsupported` when Windows.Graphics.Capture is not available on the host, and the
  state machine reports it instead of pretending to work.
* **Can this Azy see this Premiere?** The window is validated against the Premiere
  process on every important operation; an elevated Premiere cannot be covered by a
  non-elevated Azy (UIPI), which is detected and reported rather than guessed at.
* **Does the panel model match this workspace?** It is arithmetic, not a probe, and
  a wrong rectangle costs a misplaced frame rather than a wrong pixel — see below.

`core/product.cpp` still identifies the Premiere family, channel and version from
the executable's own metadata (never from a hardcoded path), and that identity is
what the log and the tray show. What it no longer does is gate visual features.

## Failure handling and Safe Mode

* Every engine operation returns a result; failures increment
  `SkinState::failures` and are recorded with `FailureTracker(threshold 3, window
  300 s)`.
* The tracker persists in `settings.ini`, so a Premiere build that made Azy fail
  before also starts in Safe Mode next time.
* On trip: `enter_safe_mode()` logs the explanation, notifies in the tray, releases
  the mirror and its GPU resources, and **stops applying the skin** until it is
  re-enabled. There is no reduced "basic" skin in 2.0.0 to fall back to: the honest
  reduced state is nothing on screen with the reason recorded, never a static
  approximation of Premiere.
* The user leaves Safe Mode from Settings → Advanced ("Re-enable features"),
  which is also the opt-in for experimental features; the next `sync()` re-applies
  the mirror.
* Azy itself crashing cannot affect Premiere: Azy holds no handles into Premiere
  and installs nothing into it. A crash simply takes the mirror window with the
  process, and Premiere never learns Azy existed.
* Premiere crashing cannot affect Azy: the tracker notices the window is gone,
  reverts, releases the surface and returns to the idle state with the observer
  watching for the next launch.

### The panel map (`core/panel_map`)

Premiere's docked panels are not windows (see [`FEASIBILITY.md`](FEASIBILITY.md)),
so "where is the timeline?" has no API answer. The panel map is the answer Azy
uses instead: a **workspace profile** of ratios and DIP heights applied to the
client rectangle at the window's DPI. Eleven panels, six profiles (Editing, Color,
Audio, Effects, Graphics and `Auto`, which resolves to Editing).

Three properties make it safe to build on:

* **It is arithmetic, not measurement.** Nothing is captured, probed or guessed
  from pixels; the only inputs are the client rectangle, the DPI and the profile.
  That is why resizing, maximising, changing monitor and 100–200% scaling all fall
  out of the same numbers.
* **It is honest about what it cannot place.** A rectangle smaller than 24 px in
  either direction is reported `usable = false` rather than decorated. A client
  area too small for a panel costs a missing region, never a broken layout.
* **It is rebuilt once per change.** The engine compares the new map with the one
  it holds and only logs/redraws when something actually moved - no timer, no
  per-frame work.

Debug mode (spec §38) reports it: the settings window lists the rectangles and the
log carries the same numbers, which is enough to correct a profile for a Premiere
version or a custom workspace.

## Extension points

| To add… | Touch |
|---|---|
| A new theme key | `core/theme_tokens.cpp` (tokens), `theme_key_name/id/from_id`, the settings combo and the tray menu's theme list |
| Support for a new Premiere release | `core/product.cpp` (`family_from_major`) and, if the workspace changed, the profiles in `core/panel_map.cpp` |
| A new theme | `core/theme_tokens.cpp` (the key table and its 13 tokens); nothing in the renderer |
| A new visual element | `resources/shaders/mirror.hlsl` plus a member in `MirrorParams`, and the same member in the cbuffer — `tools/check-mirror.py` enforces the pair |
| A new setting | `core/settings.hpp`, `Settings::clamp()`, `to_ini()/from_ini()`, `tests/core_tests.cpp`, settings window |
