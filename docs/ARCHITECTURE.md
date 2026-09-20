# Azy Skin — Architecture

## Layers

```
┌──────────────────────────────────────────────────────────────────────────────┐
│ app/          AppController · SettingsStore                                 │
│               the only place that knows about everything else; owns the      │
│               hidden message window, the tray, the settings window and all    │
│               policy decisions                                              │
├──────────────────────────────────────────────────────────────────────────────┤
│ win32/        detect/       PremiereDetector · PremiereProbe · ProcessScanner│
│               watch/        EventWatch (global WinEvent observer)            │
│               performance/  PerformanceManager (suspend policy + cadence)     │
│               skin/         WindowTracker · SkinEngine · DwmComposer ·        │
│                             CompositionSurface · GdiPlusRenderer ·           │
│                             OverlayVeil · InputGuard                          │
│               os/           win_api (optional APIs) · win_util · win_version │
│                             autostart · file_watcher                          │
│               ui/           TrayIcon · SettingsWindow · app_icon              │
├──────────────────────────────────────────────────────────────────────────────┤
│ core/         version · product · compat · theme · geometry · settings ·      │
│               failure_tracker · log · strings                                 │
│               portable C++17: no Windows headers, unit tested on any host     │
└──────────────────────────────────────────────────────────────────────────────┘
```

The `core/` layer is deliberately free of Windows dependencies, which is what
allows the version parsing, version-aware compatibility policy, theme derivation,
DPI maths, settings INI and Safe-Mode logic to be covered by ordinary unit tests
(`tests/core_tests.cpp`, 220+ assertions) on any machine.

Everything Azy knows about Premiere arrives through `detect/`:

```
PremiereDetector ── detects/identifies──► ProcessRecord { pid, exe path, version resource, ProductInfo }
                                            │
WindowTracker ──── geometry/state/DPI ──────┼──► SkinTarget
                                            │
PerformanceManager ── suspend decision ─────┼──► SuspendReason
                                            │
SkinEngine ─── consumes ────────────────────┴──► DWM frame attributes + composition surface
```

## Data flow

```
 SetupProcessDpiAwarenessContext(PMv2)
            │
            ├─ Logger (file + OutputDebugString)
            ├─ Single-instance mutex ──► second launch: post "open settings" to the first
            ├─ GdiPlusSession::start
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
                                     calls DWM and/or repaints the surface *only* when it differs
 5. arm_timer(cadence); update tray tooltip/settings status only if the line changed
```

Step 4 is the performance-critical one: `SkinEngine::apply()` builds a
`VisualKey` (window handle, frame colours, rounded/frame flags, surface rect, DPI,
radius, band thickness, shadow/glass flags and every RGBA value) and compares it
with the key it applied last time. Identical request → immediate return, with no
DWM call, no GDI+ work and no `UpdateLayeredWindow`.

## Threading model

Azy is single-threaded by design, and that is a performance decision rather than
a simplification:

| Work | Runs on | Why |
|---|---|---|
| Message loop, DWM calls, GDI+ painting, tray, settings window | the UI thread | DWM/GDI+/shell calls are cheapest there and need no synchronisation |
| WinEvent callbacks | the thread that registered the hook (UI thread) via `WINEVENT_OUTOFCONTEXT` | the handler only sets atomic flags and posts one message |
| WMI process notifications | the same STA thread, during message pumping | COM is initialised `COINIT_APARTMENTTHREADED`; callbacks arrive inside `GetMessage` and queue into a mutex-protected vector |
| `settings.ini` change detection | a thread-pool thread owned by Windows (`RegisterWaitForSingleObject`) | the callback does nothing but `PostMessage` |
| Process/window enumeration | the UI thread, only on state change | one toolhelp snapshot ≈ 1 ms and is needed only when something changed |

There is no worker pool, no render thread, no idle callback and nothing that runs
on a timer except the safety-net tick described in
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

2. **Composition surfaces** — four thin strips (top, bottom, left, right) created
   only while a skin is actually being drawn and destroyed when Premiere exits.
   Each is `WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE |
   WS_EX_TOOLWINDOW`, never extends beyond Premiere's visible frame, and is
   inserted directly above the Premiere window in the z-order (so the ring never
   covers an unrelated application). All four are painted in frame coordinates
   with `UpdateLayeredWindow`, which is why a 1px line stays exactly one pixel
   wide and the corners join seamlessly — and why the ring costs ~78 KB of
   bitmaps on a 1080p window instead of a window-sized ARGB layer.

The settings window is a third window and is created lazily, hidden, and only
when the user asks for it.

## Premiere event lifecycle

```
                  ┌─────────────────────────────────────────────────────────┐
 WMI: process     │ EventCreated/Deleted(Win32_Process)                     │
 creation event   │   ├─ name matches a Premiere executable?                │
                  │   │    ├─ yes → rescan → read identity → publish Started│
                  │   │    └─ no  → ignored                                 │
                  └─────────────────────────────────────────────────────────┘

 Started(pid, window?)  →  WindowTracker::set_window() + EventWatch watched pid/thread
                        →  sync()  →  SkinEngine::apply()  →  DWM frame + surface

 WinEvent: EVENT_OBJECT_LOCATIONCHANGE / MOVESIZE / MINIMIZE / FOREGROUND / CLOAKED
                        →  flags  →  sync()  →  WindowTracker::refresh()
                        →  PerformanceManager decision  →  apply / hide / suspend

 WMI: process deletion  →  EventKind::Stopped → skin removed, resources released
```

## Premiere version compatibility

`core/compat.hpp` holds the policy; `win32/os/win_version.cpp` probes what the
host can do by *attempting* each documented DWM attribute on a throwaway window
and checking whether DWM accepts it. The two are combined in
`resolve_features()`:

```
features = f(Premiere family & channel, host capabilities, safe mode, performance mode, experimental opt-in)
```

so a future Premiere release or a new Windows build changes behaviour by editing a
table, never by editing engine code. See [`COMPATIBILITY.md`](COMPATIBILITY.md).

## Failure handling and Safe Mode

* Every engine operation returns a result; failures increment
  `SkinState::failures` and are recorded with `FailureTracker(threshold 3, window
  300 s)`.
* The tracker persists in `settings.ini`, so a Premiere build that made Azy fail
  before also starts in Safe Mode next time.
* On trip: `enter_safe_mode()` logs the one-line explanation, notifies in the
  tray, restores the frame, and re-applies with the Safe-Mode feature set (dark
  frame only).
* The user can leave Safe Mode from Settings → Advanced ("Re-enable features"),
  which is also the opt-in for experimental features.
* Azy itself crashing cannot affect Premiere: Azy holds no handles into Premiere
  and installs nothing into it. A crash simply means the window keeps whatever
  DWM attributes it already had (a dark frame is a static appearance, not a
  behaviour).
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

Debug mode (spec §41) draws it: one screenshot of that overlay is enough to
correct a profile for a Premiere version or a custom workspace.

## Extension points

| To add… | Touch |
|---|---|
| A new theme | `core/theme.hpp` (`ThemeId`), `make_palette()`, tray menu, settings combo |
| Support for a new Premiere release | `core/product.cpp` (`family_from_major`), `core/compat.cpp` |
| A new host capability | `win32/os/win_version.cpp` probe + `core/compat.hpp` fields |
| A new visual element | a new renderer under `win32/skin/`, wired through `SkinEngine::VisualKey` — the key must gain the fields that make it distinct, or the element will never be redrawn |
| A new setting | `core/settings.hpp`, `Settings::clamp()`, `to_ini()/from_ini()`, `tests/core_tests.cpp`, settings window |
