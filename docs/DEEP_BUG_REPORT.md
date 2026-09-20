# Deep bug report

Every defect found by the deep second-pass review, with root cause, impact, fix
and how the fix was checked. Ordered by severity, then by the order they were
found.

Severity scale: **P0** data loss / crash / security / harm to Premiere ·
**P1** a core promise is broken, or a blocking call on the UI thread ·
**P2** a real defect with limited user impact, or a slow resource leak ·
**P3** polish, waste, or documentation accuracy.

Status scale: **Fixed** (in the tree, rebuilt, tests green) · **Open** ·
**Won't fix** (with reason).

Verification words are used strictly: **VERIFIED** = a machine checked it,
**STRONGLY SUPPORTED** = API documentation plus code evidence, no runtime,
**UNVERIFIED** = needs Premiere on Windows, **HIGH RISK** = unresolved.


> **Scope note (v1.3.0):** this document is the v1.2.3 deep review. It covers the static layers
> (frame colours, the ring, the sheet) and is still accurate for them. The duplicate
> window added in v1.3.0 is covered by
> [`AZY_OVERLAY_ARCHITECTURE.md`](AZY_OVERLAY_ARCHITECTURE.md) and
> [`AZY_OVERLAY_TEST_PLAN.md`](AZY_OVERLAY_TEST_PLAN.md), and carries its own claim
> label: IMPLEMENTED — RUNTIME UNVERIFIED.

---

## DB-1 — A blocking cross-process window-text query on every window of the desktop

| | |
| --- | --- |
| **Severity** | **P1** |
| **Location** | `src/win32/detect/premiere_probe.cpp`, `enum_windows_proc` (line ~42 before the fix) |
| **Root cause** | The enumerator used to score candidates by window title and class. That scoring was removed and the two values were left computed and discarded: `const std::wstring title = window_text(hwnd); … (void)title;` |
| **Why it happens** | `window_text()` calls `GetWindowTextLengthW` + `GetWindowTextW`. For a window owned by another process those are **`WM_GETTEXT` sends**: the caller blocks until the owning UI thread pumps messages. `EnumWindows` visits every top-level window on the desktop, so Azy called this for every window of every application. |
| **Potential user impact** | Any application on the machine that is busy or hung — an installer, a game, an IDE mid-build, a "Not responding" dialog — stalls Azy inside its own message loop. The tray stops answering, the settings window freezes, the skin stops following Premiere, and Windows may badge Azy as "Not Responding". It happens on every rescan, which was up to 5×/second. |
| **Fix** | The discarded fetch is gone entirely, and the enumerator now filters by pid **first**, so all remaining per-window work (the DWM cloak query, `GetWindowRect`, style reads) happens only for Premiere's own windows — a handful instead of hundreds. |
| **Verification method** | `grep -rn "GetWindowTextW" src/` returns no callers; `window_text()` was deleted because nothing used it after the fix. Rebuilt clean (zero warnings from `src/` + `include/`); `verify.sh` 6/6 at that time (7/7 as of v1.3.0). |
| **Current status** | **Fixed.** Residual risk: none identified — the call no longer exists in the codebase. |

---

## DB-2 — The idle scan storm: up to five process snapshots per second on a busy desktop

| | |
| --- | --- |
| **Severity** | **P1** (attacks the stated 0–1 % idle CPU requirement) |
| **Location** | `src/win32/watch/event_watch.cpp` (`relevant`, idle branch) + `src/app/app_controller.cpp` (sync step 1) + `premiere_detector.hpp` (`kMinScanIntervalSeconds = 0.20`) |
| **Root cause** | While nothing is tracked, `EventWatch::relevant()` returns true for **every** window event on the desktop ("idle: observe everything"), and the controller turned every wake-up into `note_window_activity()`. The detector refreshed its view of the process list at most 5×/second (`kMinScanIntervalSeconds`), and a refresh is a full `CreateToolhelp32Snapshot` walk of every process plus, per candidate, `OpenProcess` and a version read. |
| **Why it happens** | The event filter was written to be safe ("never miss the event that tells us Premiere started") without a matching policy for how expensive acting on every event is. The throttle limited a burst; it did not stop a sustained stream, and a desktop with applications animating or a shell notification every second produces exactly that. |
| **Potential user impact** | Sustained CPU use while Azy sits idle in the tray, precisely what the brief forbids. On a machine with WMI unavailable (rare, but real: WMI disabled by policy, or a broken repository) the fallback path was also the noisy one. |
| **Fix** | Two parts. (1) The detector now decides whether window activity is worth acting on: `wants_window_activity()` returns true only when a target exists but its window has not appeared yet, or when there is no target **and** no process observer to announce one. (2) The fallback cadence when nothing is tracked and no observer exists is 1 s (`kIdleScanIntervalSeconds`) instead of 0.2 s. |
| **Why this shape and not a bigger change** | The event filter was deliberately left alone. Making the filter itself drop events would have created a new failure mode (a missed window-creation event while a target is known but windowless would mean the skin never attaches); moving the decision into the detector keeps every event and only removes the *pointless* work. |
| **Verification method** | By inspection against every path that can set the flag: `note_window_activity()` is still called when a target lacks a window (attach still works), and the WMI path still forces an immediate scan (`pending_is_process_event_` → interval 0). Build clean, tests green. A stopwatch measurement is **UNVERIFIED** (needs Windows). |
| **Current status** | **Fixed.** Expected residual: with WMI live and no target, zero scans while idle. |

---

## DB-3 — A GDI leak: one device context and one DIB section leaked per resize

| | |
| --- | --- |
| **Severity** | **P2** (slow leak that ends in a functional failure) |
| **Location** | `src/win32/skin/gdiplus_renderer.cpp`, `GdiPlusRenderer::release()` and the GDI+ failure path in `ensure_size()` |
| **Root cause** | `ensure_size()` selected the DIB into the memory DC (`SelectObject(memory_dc_, bitmap)`) and kept the returned previous object in a local variable that was never stored. `release()` then deleted the GDI+ wrapper, called `DeleteDC(memory_dc_)` with the DIB still selected, and called `DeleteObject(bitmap_)`. A DC cannot be deleted while a bitmap is selected into it, and that bitmap cannot be deleted while it is selected: **both calls fail and both handles leak.** |
| **Why it happens** | Each of the calls is individually correct-looking; the failure is only visible when you know the Windows rule that a DC must be returned to its default state before deletion. |
| **Potential user impact** | Every geometry change — resize, maximise/restore at a different DPI, moving between monitors with different scaling — leaked a device context and a DIB. GDI handles are a per-process quota (10,000 by default). A long session with lots of window movement would eventually exhaust it: `CreateDIBSection`/`CreateCompatibleDC` start failing, the ring stops being drawn, `present()` reports failures, and the failure counter can drive Azy into Safe Mode — a symptom that looks like "the skin randomly stopped working" and is impossible to diagnose without knowing this. |
| **Fix** | The previous object is remembered (`previous_bitmap_`) and selected back before either deletion, in `release()` and in the GDI+-bitmap failure path. |
| **Verification method** | Inspection of every path that selects the DIB: exactly one `SelectObject` site, one restore site, both before the deletes. Build clean, tests green. Handle-count measurement is **UNVERIFIED** (needs Windows and a resize loop). |
| **Current status** | **Fixed.** |

---

## DB-4 — A recycled window handle could be restyled, or "restored", on another application

| | |
| --- | --- |
| **Severity** | **P2** |
| **Location** | `src/win32/skin/window_tracker.cpp` (`refresh`), `src/win32/skin/dwm_composer.cpp` (`apply`, `revert`) |
| **Root cause** | Both paths validated the stored `HWND` with `IsWindow()` only. Windows recycles handle values: a handle that named Premiere's frame a moment ago can name an unrelated window now, and `IsWindow()` answers "yes" for the recycled handle. |
| **Why it happens** | The gap between "Premiere's window was destroyed" and "the detector notices the process is gone" is normally small but not zero — a WMI event can be delayed, the fallback scan runs at most on the safety-net cadence, and a destroyed window's handle can be reused almost immediately by a busy desktop. |
| **Potential user impact** | Azy would apply a dark title bar and frame colours to *another application's* window (an Explorer window, a dialog), draw its ring around it, and — on detach — "restore" attributes on a window it never touched. Visible, embarrassing, and a modification of a window Azy has no business modifying, which the brief explicitly rules out. |
| **Fix** | New `win_util::window_belongs_to(hwnd, pid)` (liveness **and** owner check; pid 0 means "unknown owner, liveness only"). The tracker drops the target when ownership no longer matches (with a log line), and the DWM composer refuses to apply to — or revert on — a handle that is no longer Premiere's, storing the pid with the saved frame state. |
| **Verification method** | Inspection of every call site that receives a stored handle; the ring/veil placement paths are covered because the tracker clears the target, and `SkinEngine::apply` nulls its `target_` when `request.target.hwnd` is empty. Build clean, tests green. |
| **Current status** | **Fixed** for the two paths that *modify* another window or draw around it. Note: `SkinEngine::reassert_stacking` still uses `IsWindow` on its stored target — it only moves Azy's own windows, and the tracker's check clears the target on the next event or safety-net tick, so the exposure is a frame of misplaced decoration at worst. |

---

## DB-5 — The dead "window count" enumerated the whole desktop once per second

| | |
| --- | --- |
| **Severity** | **P2** (pure waste, every second, forever) |
| **Location** | `src/win32/skin/window_tracker.cpp` (`kWindowCountIntervalSeconds`, the count block), `SkinTarget::window_count`, `PremiereProbe::find_top_level_windows` |
| **Root cause** | A diagnostic counter was computed and stored, and nothing ever read it. |
| **Why it happens** | It was written for a diagnostics panel that was later designed around different data. The field stayed, and with it the work: an `EnumWindows` over every top-level window on the desktop, a `std::vector<HWND>`, and — the expensive part — one `DwmGetWindowAttribute(DWMWA_CLOAKED)` per *visible* window, i.e. a round trip to the DWM process for every window on screen, once per second. |
| **Potential user impact** | Permanent, useless CPU and DWM traffic while a skin is attached — tens of cloaked-state queries per second on a typical desktop, hundreds on a busy one. |
| **Fix** | Field, constant, block and the now-unused `find_top_level_windows` helper all deleted. |
| **Verification method** | `grep -rn "window_count" src/ include/ tests/ tools/` → no results. Build clean, tests green. |
| **Current status** | **Fixed.** |

---

## DB-6 — The DWM dark frame was not un-applied when the feature was switched off

| | |
| --- | --- |
| **Severity** | **P3** |
| **Location** | `src/win32/skin/dwm_composer.cpp`, `apply()` |
| **Root cause** | The dark-frame branch only acted when the feature was enabled. With the feature already applied, turning it off (performance mode, Safe Mode, a per-feature override in `settings.ini`) left the attribute set until detach. |
| **Why it happens** | The revert path handles "Premiere is gone" and "the skin is off"; it did not handle "this one feature is off while the rest stays". |
| **Potential user impact** | Premiere's title bar stays dark while Azy reports — in the tray, in Settings and in the log — that the frame treatment is off. A contradiction between the UI and what is on screen is exactly what erodes trust in a tool like this. |
| **Fix** | An `else if (applied_.dark)` branch restores the saved value (or the DWM default) and clears the applied flag. |
| **Verification method** | Inspection of the state machine: every `applied_*` flag now has exactly one set site and at least one clear site, and `revert()` is consistent with the new state. Build clean, tests green. |
| **Current status** | **Fixed.** |

---

## DB-7 — Dangling stack pointer in the version-string fallback

| | |
| --- | --- |
| **Severity** | **P3** (undefined behaviour on a rarely-taken path) |
| **Location** | `src/win32/detect/premiere_probe.cpp`, `read_file_version` |
| **Root cause** | `wchar_t dynamic_query[128]` was declared **inside** the `if` block that decided whether a translation table existed, while `const wchar_t* query` was declared outside it and used after the block. When the block ran, `query` pointed at a buffer whose lifetime had ended. |
| **Why it happens** | A classic scope mistake: the assignment looks like it extends the buffer's life, and the compiler does not diagnose it at the default warning level. |
| **Potential user impact** | `VerQueryValueW` would read a dead stack frame. Usually the bytes are still intact and nothing looks wrong; when the compiler reuses that stack slot (a different build, a different optimisation level, another call between), the query string is garbage: the version read fails (conservative fallback — acceptable) or the function scans past the end of the buffer looking for a terminator. |
| **Fix** | The buffer is declared in the enclosing scope. |
| **Verification method** | Inspection of the scope; the fallback path is only reached when `VS_FIXEDFILEINFO` is missing. Build clean. Runtime behaviour on a machine with such an executable is **UNVERIFIED** (and Premiere's own binaries do carry fixed file info, so this path is nearly always skipped). |
| **Current status** | **Fixed.** |

---

## DB-8 — The whole-window veil allocated a brush inside every paint

| | |
| --- | --- |
| **Severity** | **P3** |
| **Location** | `src/win32/skin/overlay_veil.cpp`, `WM_PAINT` |
| **Root cause** | `CreateSolidBrush` + `DeleteObject` on every paint, for a window that paints one colour and changes it only when the theme or a slider changes. |
| **Why it happens** | Defensive coding ("create what this paint needs") applied to a value that never varies. |
| **Potential user impact** | Negligible in practice (paints are per change, not per frame) — but it is a per-paint allocation for a constant, and this review's standard is that a constant is created once. |
| **Fix** | The brush is cached and rebuilt only when the colour changes; it is released with the window. |
| **Verification method** | Inspection; build clean. |
| **Current status** | **Fixed.** |

---

## DB-9 — Documentation and dead code

| | |
| --- | --- |
| **Severity** | **P3** |
| **Location** | `src/` (dead helpers), `docs/`, `tools/` |
| **Root cause** | Accumulated from earlier passes. |
| **Details** | `window_text()` and `PremiereProbe::find_top_level_windows()` had no callers left after the fixes above and were deleted; the audit's own claim that Azy has "no thread of its own" was imprecise, because the settings file watcher uses a thread-pool wait (`RegisterWaitForSingleObject`) — the callback only posts a message, but the wording was corrected in `docs/AZYSKIN_AUDIT.md`; the v1.2.2 changelog said "six defects" where the fix table lists seven. |
| **Fix** | Dead code removed; wording corrected in the audit and in the changelog. |
| **Current status** | **Fixed.** |

---

## Checked and found sound (no defect)

These were suspected during the review and cleared with evidence, so that the
report does not read as if only failures were looked for.

| Area | Finding |
| --- | --- |
| Double-blended corners | **Not possible by construction.** `ring_strip_rects` gives the corner arcs to the horizontal strips and insets the vertical ones, so no pixel belongs to two strips. Asserted by `test_ring_layout`. |
| Event-hook feedback loop | Impossible: every hook is registered with `WINEVENT_SKIPOWNPROCESS`, so Azy's own windows cannot wake it. |
| Duplicate hook registration | `EventWatch::start()` returns early if the hooks exist; `stop()` unhooks all three and nulls them. |
| Azy stealing focus or activation | No `SetForegroundWindow`, `SetActiveWindow`, `SetFocus`, or activating `ShowWindow` anywhere in `src/`. Placement uses `SWP_NOACTIVATE`/`SW_SHOWNA`. **VERIFIED** by grep. |
| Azy becoming a global always-on-top window | `WS_EX_TOPMOST` is never *set*, only read: the anchor logic mirrors Premiere's band (topmost only if Premiere is topmost). **VERIFIED** by grep. |
| WM_ERASEBKGND flicker on the layers | Both layered windows answer `WM_ERASEBKGND` with 1 and paint only on change. |
| WMI COM reference counting | The two `AddRef`s in `subscribe()` are balanced by the explicit `Release`, the internal `Release`, and WMI's own release after `CancelAsyncCall`; no double-free path exists because every pointer is nulled after release. |
| Settings store durability | Writes go through a temp file + `MoveFileEx(REPLACE_EXISTING\|WRITE_THROUGH)`; a crash cannot leave a half-written configuration. |
| Layout safety | `test_layout_safety` and `test_ring_layout` cover degenerate sizes; a panel that will not fit is marked unusable and skipped, never drawn at a negative size. |
| Colour safety | The veil alpha is capped at 0.60 (`0.05 + 0.55 × strength`), so no slider position can make Premiere invisible. |

---

## Summary

| Severity | Count | Fixed | Open |
| --- | --- | --- | --- |
| P0 | 0 | — | — |
| P1 | 2 | 2 | 0 |
| P2 | 3 | 3 | 0 |
| P3 | 4 | 4 | 0 |

No P0 defect (crash, data loss, harm to Premiere, security) was found, and none
of the four "logical impossibility" classes the review was asked to hunt for
(§39 of the brief) is present: every API used is documented to do what the code
expects of it, no path waits for an event that is never emitted, no code attempts
to style pixels Azy does not own, and no path assumes a child window exists.

The two P1s were both in the same subsystem — the discovery path that runs on
every event — and both were invisible in the source's shape: one was a leftover
of a removed scoring heuristic, the other a policy gap between an event filter and
a throttle. Neither would have shown up in a compiler warning, a unit test, or a
casual read.
