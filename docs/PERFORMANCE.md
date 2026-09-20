# Azy Skin — Performance

Performance is treated as a correctness requirement, not a tuning exercise. The
rule used throughout the codebase is:

> If it costs CPU while nothing is happening, it does not ship.

---

## Budget

| Situation | Target | What Azy actually does |
|---|---|---|
| Premiere not running | **0% CPU** | Nothing is scheduled. The safety-net timer (1 s) runs a few atomic reads and returns. No window enumeration, no DWM calls, no painting |
| Premiere open, nothing changing | **0% CPU** | Same: no events arrive, so no work happens at all between ticks. `SkinEngine::apply()` returns immediately on a matching visual key — no DWM call, no repaint |
| Typing / clicking in Premiere | **0% Azy CPU** | Azy is not in the input path at all: `WS_EX_TRANSPARENT` means its surface is never even hit-tested |
| Premiere being dragged/resized | **Near 0%** | `EVENT_SYSTEM_MOVESIZE_START` hides the surface; the frame attributes need no help because Windows moves them with the window. One repaint after the movement settles |
| Premiere launched | one short burst | Process identification (one toolhelp snapshot + one version-resource read) then one apply. Then idle again |
| Memory (working set) | **< 100 MB target** (expected ≈ 6–10 MB) | See "Memory" below |
| GPU | **zero** | No D3D, no DirectComposition, no video engine; one GDI+ bitmap handed to DWM |

---

## What runs, and when

Azy's entire runtime is **event-driven**. There is no polling loop, no
animation frame, no screen capture and no idle callback.

| Trigger | Source | Work performed |
|---|---|---|
| Premiere process created/deleted | WMI `Win32_Process` events (optional) → posts a sync message | one process snapshot, one identity read, one apply |
| Window created / destroyed / shown / hidden / state or location changed | `SetWinEventHook` (out-of-context) → posts a sync message | read window geometry, DPI, monitor, state; re-apply only if the visual key changed |
| Move/size loop started/ended | `EVENT_SYSTEM_MOVESIZE_*` | hide surface / repaint once after settling |
| Minimise start/end | `EVENT_SYSTEM_MINIMIZE*` | suspend / resume |
| Foreground change | `EVENT_SYSTEM_FOREGROUND` | re-read foreground state (only used if "suspend while inactive" is on) |
| Desktop switch, cloak/uncloak | `EVENT_SYSTEM_DESKTOPSWITCH`, `EVENT_OBJECT_CLOAKED` | suspend / resume |
| Settings file edited | directory change notification (thread-pool wait) → message | reload the INI |
| Windows theme/DPI/display change, resume from sleep | `WM_SETTINGCHANGE`, `WM_DISPLAYCHANGE`, `WM_POWERBROADCAST` | re-resolve host capabilities, re-apply |
| Safety-net timer | `SetTimer`, **1 s** (2 s while suspended) | read a few atomics; normally returns immediately |

The safety-net timer exists for one purpose: to recover from a notification
Windows did not deliver (a lost `MOVESIZE_END`, a window destroyed while the
event queue was saturated). Its normal cost is a handful of atomic loads — no
syscalls, no enumeration, no drawing. Raising it to 5 seconds changes nothing
observable except how quickly such an edge case is noticed; it is not a polling
frequency in the usual sense.

### Rules the code follows

1. **No work without a change.** `SkinEngine::apply()` compares the complete
   desired visual state (`VisualKey`) against what is already on screen and
   returns `false` immediately when they match.
2. **One repaint per change, not per frame.** The ring bitmap is regenerated only
   when the window rect, DPI, monitor, radius, band size, shadow/glass flags or
   any colour changes. Whether that happens once or twice per user action, Azy
   never repaints while the user does nothing.
3. **Fixed-cost drawing.** Shadow falloff uses a fixed bucket count (derived from
   the band thickness, not the window size), so a 4K window costs the same as a
   small dialog. The ring is a handful of GDI+ paths, not a per-pixel loop.
4. **Bounded memory.** The ring is four thin strips (top, bottom, left, right),
   each only as thick as the treated band plus the corner arc — not a
   window-sized ARGB layer. Per-frame compositing by DWM is limited to those
   strips as well.
5. **Coalesced wake-ups.** A burst of 200 window events produces exactly one sync
   message, and a window-event burst is additionally throttled to one rescan per
   200 ms.
6. **Suspend means stop.** Suspended states (skins off, minimised, inactive,
   dragging, cloaked) hide the surface, skip the DWM work, and drop the timer to
   2 s.

---

## Memory

| Component | Cost |
|---|---|
| Executable on disk | ~440 KB (Release, x64) |
| Private working set | ~6–10 MB (Win32 + GDI+ + C++ runtime, no framework) |
| Surface bitmaps | the ring is four thin strips, not a window-sized layer: ~2 × width × thickness + 2 × height × thickness pixels × 4 bytes. At 1920×1080/100% DPI that is ~78 KB; at 3840×2160/200% DPI (thickness 40px) ~920 KB. A single window-sized layer would be ~33 MB at 4K |
| GDI+/DWM resources | 4 DIB sections, 4 memory DCs, 4 layered windows, 1 tray icon — all released when Premiere exits |
| Settings/tracker/log buffers | a few KB |

For comparison, an Electron-based equivalent with the same visual result would
start at roughly 80–150 MB and a helper process — which is exactly why Azy is
plain Win32 with GDI+ for one bitmap.

There is no caching layer, no image atlas, no script engine and no UI toolkit.

---

## What was deliberately excluded for performance

* No animation of any kind (no transitions, no hover effects, no glow) — the
  brief's requirement and also the single biggest saving.
* No `ImmersiveColorSet`/`SetWindowCompositionAttribute` blur hacks (an
  undocumented `user32` call with historically poor behaviour and cost).
* No screen capture, no DWM thumbnail APIs, no `PrintWindow` sampling.
* No per-panel surfaces (would multiply both work and failure modes; see
  [`LIMITATIONS.md`](LIMITATIONS.md)).
* No continuous `DwmGetWindowAttribute` sampling: geometry is re-read on
  notifications, not on a loop.
* No framework. No dependency beyond Windows itself and the C++ standard library.

---

## Performance mode

Settings → Performance → **Performance mode** applies the cheapest possible
version of the skin for lower-end machines:

* Translucency removed (surfaces become opaque flat fills — no per-pixel alpha
  blending beyond the ring itself).
* Shadow removed entirely (no multi-stroke falloff pass).
* Rounded surface corners disabled (square ring, fewer path segments).
* Experimental features forced off.
* Timer cadence floor of 1 s (no faster reactions).

What it does **not** do is drop the DWM frame treatment — those attributes are
free once set, and removing them would make the skin pointless.

---

## How to verify on Windows

1. **Idle.** Open Premiere, leave it maximised and untouched for 5 minutes.
   Task Manager → Details → `AzySkin.exe`: CPU should read `0%` (Task Manager
   rounds to whole percent; use Process Explorer and look at the CPU History
   column for a flat line). Memory (private working set) should be single-digit MB.
2. **Interaction.** Drag the timeline, scrub, play, resize panels: `AzySkin.exe`
   CPU should stay at or near 0%. Premiere's own CPU/GPU should be
   indistinguishable from a session without Azy (compare the same project with
   Azy running vs. exited).
3. **Window operations.** Drag and resize the Premiere window across monitors:
   Azy's surface should disappear during the drag, then snap into place once you
   release. Count how long the burst lasts: it should be one repaint per release,
   not a stream.
4. **Suspension.** Minimise Premiere: Azy's working set should drop slightly and
   CPU should remain 0%. Restore: the ring reappears without a frame flash.
5. **Resource leaks.** Move and resize Premiere 200 times, then check GDI
   Objects and User Objects in Performance Monitor (`Process\GDI Objects`,
   `Process\Handle Count`) for `AzySkin.exe` — both should be flat, not climbing.
   `docs/TESTING.md` has the exact counters and thresholds.
