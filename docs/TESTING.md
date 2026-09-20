# Azy Skin — Testing

Automated tests cover the portable core (`tests/core_tests.cpp`, 280+ assertions
run by `ctest`) and CI compiles the complete Windows application (MSVC with the
resource compiler, plus a cross-compile gate). What cannot be automated is the
behaviour of the skin on a real Windows desktop with a real Premiere Pro running.

This document is that manual test plan. Work through it on a Windows machine;
every row has an observable pass criterion, and every failure-case row names the
log line that should accompany it.

---

## 0. Setup

* Windows 10 1809+ or Windows 11, x64.
* Premiere Pro 2024 / 2025 / 2026 installed and licensed.
* At least one project with media on the timeline.
* For multi-monitor/DPI rows: two monitors with different scaling (e.g. 100% and
  150%), or one monitor whose scaling you can change between runs.
* Log file: `%LOCALAPPDATA%\Azy Skin\azy.log` (Settings → *Open log file*).

Before starting: delete `%LOCALAPPDATA%\Azy Skin\` to test first-run behaviour.

---

## Reading the log

A healthy attach looks like this (one line pair, and nothing else until something
changes - Azy is event-driven, so a quiet log is a working log):

```
INFO  Premiere detected: Premiere Pro 2026, version 26.0.0.72
INFO  Azy Skin: Azy Dark Glass, full - active | Premiere Pro: Premiere Pro 2026 26.0.0.72
INFO  ring: 1920x1040 frame at (0,0), 12px thick, band 10px, radius 8px, 288 KB, strongest pixel alpha 199, above Premiere: yes
```

The `ring:` line is the one to read when a user reports "the skin does nothing":

| Field | Meaning |
|---|---|
| `1920x1040 frame at (0,0)` | the rectangle the ring was drawn on. A maximized window is drawn on the monitor **work area** (a window that hangs over the display would otherwise put the ring off screen); a windowed one on its reported frame |
| `strongest pixel alpha` | the strongest pixel the renderer produced. `0` means the bitmap came out fully transparent - a rendering fault, not a placement one |
| `above Premiere: yes` | the strips sit in front of the Premiere window in the z-order. `no` means something is covering them (a lower-integrity process cannot be placed above a higher-integrity one), and the skin is invisible no matter how well it was rendered |

| `strips misplaced: 0` | strips Windows did not leave where they were put. Anything but `0` means the ring is in the wrong place while every call reported success |

The same two facts are on screen, under the status lines of the settings window, so a
user report can carry them without a log file:

```
Azy Skin 1.0.2 - window 'Premiere Pro' 1920x1040 at (0,0) | maximized | screen (0,0)-(1920,1080) | 100%
Ring 12px at (0,0)-(1920,1040) | brightest pixel 199/255 | in front of Premiere: yes
```

Whenever the window's geometry changes, the *reasoning* behind that rectangle is in
the log as one more line:

```
window frame: reported (0,0)-(1920,1040) [dwm], window (-8,-8)-(1928,1048) [getwindowrect],
ring on (0,0)-(1920,1040), screen (0,0)-(1920,1080), work area top 0 bottom 1040, maximized, 96 dpi
```

### "The skin does nothing" - in order

1. Is the version in the panel the one you installed? The first diagnostic line
   always names it.
2. `Ring: not drawn yet` - no Premiere window is attached yet, or the window is
   smaller than 200x200. The line above names the window Azy attached to.
3. `Ring: not on screen - ...` - the reason is spelled out: z-order blocked
   (Premiere runs elevated: start Azy as administrator too), the bitmap was empty,
   the frame was empty.
4. `brightest pixel 0/255` - a rendering fault; the bitmap came out transparent.
5. `in front of Premiere: no` - something is stacked above the strips. A tray
   notification says so as well.
6. Everything looks healthy but you still cannot see the ring: it is drawn on the
   rectangle in the second line, in the colours of the selected theme. Raise
   *Border intensity* and *Overall darkness* in Settings - the defaults are
   deliberately subtle. On a maximized window the ring follows the monitor work
   area (not the invisible border that hangs off the screen).

---

## 1. Startup and detection

| # | Steps | Pass criteria |
|---|---|---|
| 1.1 | Launch `AzySkin.exe` with no Premiere running | Tray icon appears. No window, no taskbar button, no Alt+Tab entry. Log: `Azy Skin 1.0.2 starting`, `host: Windows ...`, `WinEvent observer started` |
| 1.2 | Now start Premiere Pro | Within ~1 s of the editor window appearing: log shows `Premiere detected: Premiere Pro 2025, version 25.6.0.58` and the skin is applied. Settings → Treatment shows `full`, and the headline says `active`. The log line `ring: ... strongest pixel alpha <n>, above Premiere: yes` appears with n > 0 |
| 1.3 | Start Premiere **first**, then start Azy Skin | The skin appears without restarting Premiere (startup sync picks it up immediately) |
| 1.4 | Launch Azy Skin a second time | No second instance; the existing instance's Settings window appears. Log: `another Azy Skin instance is already running` |
| 1.5 | Close Premiere (keep Azy running) | Skin disappears, frame returns to normal. Log: `Premiere Pro closed (pid …) - releasing skin resources`, then `skin resources released`. Tray tooltip says `Premiere Pro: not running` |
| 1.6 | Restart Premiere | Skin reapplies automatically (no Azy interaction) |
| 1.7 | Start `Adobe Premiere Pro Beta.exe` alongside the release build | Azy re-targets the Beta build; treatment is `reduced` if the Beta build is unknown (no rounding) |
| 1.8 | Kill Premiere from Task Manager (crash simulation) | Identical to 1.5: Azy survives, logs the release, and is ready for the next launch |

## 2. Toggle (ON/OFF)

| # | Steps | Pass criteria |
|---|---|---|
| 2.0a | Settings → Appearance → uncheck *Cover the whole window* | The tint over the whole window disappears immediately; the edge ring stays. Log: `overlay: … veil …` is no longer followed by anything until it is re-enabled |
| 2.0b | Re-enable it and drag *Overlay strength* 0 → 100% | The tint deepens smoothly and immediately (no animation, no repaint storm: one `SetLayeredWindowAttributes` call and one fill per change). At 100% the layer reaches 60% alpha; at 0% only the ring remains |
| 2.0c | With the overlay on, click into the Premiere timeline, drag a clip, scroll, use shortcuts | Nothing is intercepted: the overlay is click-through (`WS_EX_TRANSPARENT`), never activated (`WS_EX_NOACTIVATE`) and never appears in Alt+Tab |
| 2.1 | Single left-click the tray icon (skin ON → OFF) | Frame reverts **immediately** and exactly to its previous appearance (title bar colour, corner rounding, border). Log: `skin removed (skin disabled)`, `skin disabled by the user` |
| 2.2 | Single left-click again | Skin returns instantly. No Premiere restart, no flicker beyond one apply |
| 2.3 | Toggle 20 times in a row | No leakage (see §8), no flicker between states, no delay |
| 2.4 | Tray → Theme → `Original` | All Azy changes removed; Premiere looks stock. Log: `theme changed to Original` |
| 2.5 | Tray → Theme → `Azy Dark` | Opaque treatment applied (no translucency visible in the band) |
| 2.6 | Tray → Theme → `Azy Dark Glass` | Glass treatment back |
| 2.7 | Settings window → uncheck *Enable skin* | Same as 2.1, and the tray menu shows *Skin Enabled* unchecked |
| 2.8 | Tray → *Suspend Skin* | Skin removed without changing the ON/OFF setting; tray shows *Suspend Skin* checked, tooltip says `suspended`. Unchecking restores it |

## 3. Window states

| # | Steps | Pass criteria |
|---|---|---|
| 3.1 | Premiere maximized | Ring follows all four edges exactly, 1px on each edge. Corner rounding skipped (frame is square when maximized — matches Windows behaviour) |
| 3.2 | Premiere windowed, moved around | Ring stays aligned to the visible frame edge during and after the move. Surface hidden while dragging, repainted once after release |
| 3.3 | Premiere resized from a corner | Same; no tearing, no lag in Premiere's own resize |
| 3.4 | Premiere minimised | Surface hidden, no CPU work. Log: `Premiere minimized - pausing skin work` |
| 3.5 | Premiere restored | Ring reappears at the correct geometry. Log: `Premiere restored - resuming skin work` |
| 3.6 | Switch to a virtual desktop with Premiere, then back | Surface hidden while away, restored on return (cloak events) |
| 3.7 | Premiere fullscreen (borderless, covering the screen) | Ring sits on the screen edge; no rounding; no gap |
| 3.8 | Maximize → restore → maximize rapidly | No stale ring left behind, no flicker loop |
| 3.9 | Let the screen lock, then unlock | Skin intact afterwards; log may show `resumed from sleep` |

## 4. Multi-monitor and DPI

| # | Steps | Pass criteria |
|---|---|---|
| 4.1 | Move the Premiere window between two monitors | Ring repositions exactly; log shows the target moving (`Premiere moved to NNN DPI (N%)`) when scaling differs |
| 4.2 | Run the whole suite at 100%, then 125%, 150%, 175%, 200% | The 1px hairline is exactly one pixel at every scale; nothing is blurry; nothing is offset by a half pixel |
| 4.3 | Change Windows scaling while Premiere is open | `WM_SETTINGCHANGE` → skin re-applies at the new scale |
| 4.4 | Put the taskbar on the secondary monitor | Ring follows the *Premiere* monitor, not the primary one |
| 4.5 | Disconnect a monitor with Premiere on it | Ring returns with the window; no stuck ring at the old coordinates. Log: `display configuration changed` |
| 4.6 | Mixed-DPI: two monitors 100% and 200%, window straddling both | Ring follows the monitor the window's centre is on; no double-drawn border |

## 5. Editing interactions (the skin must be invisible here)

| # | Action | Pass criteria |
|---|---|---|
| 5.1 | Click anywhere inside Premiere, including right at the window edge and inside the Azy band | The click reaches Premiere. No click is swallowed, no focus change |
| 5.2 | Keyboard shortcuts (space, `C`, `V`, `Ctrl+K`, `Ctrl+Z`) | All work; Premiere keeps keyboard focus at all times |
| 5.3 | Mouse wheel over panels and timeline | Scrolling unaffected, including over the band |
| 5.4 | Drag clips, in/out points, playhead scrubbing | Unaffected; no stutter attributable to Azy |
| 5.5 | Drag panels to redock, resize panel dividers | Unaffected; Premiere's own layout changes are not interfered with |
| 5.6 | Open context menus inside Premiere, then close them | Menus open and close correctly; Azy never appears above them |
| 5.7 | Type in a Premiere dialog (e.g. Export Settings) | No focus stealing, no input delay |
| 5.8 | Drag the Premiere window by its title bar and release over another app | Surface hidden while dragging, correct on release, nothing left on top of the other app |
| 5.9 | Full playback of a timeline for 5 minutes | Premiere's playback performance identical to a session without Azy (compare dropped-frame indicators) |

## 6. Focus discipline

| # | Steps | Pass criteria |
|---|---|---|
| 6.1 | While working in Premiere, watch the foreground window | Premiere remains foreground; Azy never activates |
| 6.2 | Alt+Tab | Only Premiere (and other real apps) listed — never Azy, never its surface |
| 6.3 | Click the tray icon, then check the active window | Premiere stays/returns as the foreground app |
| 6.4 | Open Azy Settings, then click into Premiere | Settings loses focus normally; the skin stays applied |

## 7. Failure cases

| # | Steps | Pass criteria |
|---|---|---|
| 7.1 | Kill Azy Skin (Task Manager) while Premiere runs | Premiere continues normally. Frame keeps its last appearance (a static dark frame is not a dependency). Relaunching Azy takes over cleanly |
| 7.2 | Exit Azy from the tray | Frame is restored to its exact pre-Azy values; log: `restoring Premiere's frame and releasing resources`, `Azy Skin stopped` |
| 7.3 | Corrupt `settings.ini` (e.g. `garbage===!!!`) and start Azy | Azy starts with defaults, logs a warning, and rewrites a valid file |
| 7.4 | Set `theme=rgb_gaming` in `settings.ini` | Warning logged, defaults used, no crash |
| 7.5 | Edit `settings.ini` by hand while Azy runs | Reload within ~1 s; log: `settings file changed on disk; reloading` |
| 7.6 | Enter Safe Mode (set `safe_mode=1` + `failures=3` in the INI, or simulate failures) | Tray notification appears; Treatment shows `safe mode`; only the dark frame is applied; log contains `Safe Mode has been enabled` |
| 7.7 | Settings → *Re-enable features* | Safe mode cleared, full treatment returns, log notes the change |
| 7.8 | Restart Windows with Premiere open in the session | Azy starts with Windows (if enabled), detects Premiere, applies the skin, nothing blocks the shutdown |
| 7.9 | Manually delete the HKCU Run entry while *Start with Windows* is checked | Tray still shows it checked until toggled; unchecking/rechecking recreates it correctly (documented behaviour, no crash) |
| 7.10 | Run with two Premiere versions installed, start the non-default one | Azy identifies and attaches to whichever is actually running (no path assumptions) |

## 8. Resource discipline (leak checks)

Use Performance Monitor (`perfmon`) with counters for `AzySkin.exe`:

| Counter | Procedure | Threshold |
|---|---|---|
| `Process\Private Bytes` | 30 min idle with Premiere open | Flat (no upward trend). Expect single-digit MB |
| `Process\Handle Count` | 200 move/resize cycles + 20 skin toggles | Flat; no growth per cycle |
| `GDI Objects` (Task Manager → Details, add column) | same as above | Flat; expect < 20 |
| `User Objects` | same as above | Flat; expect < 20 |
| `Process\% Processor Time` | 5 min idle, then 5 min of active editing | `0%` idle; indistinguishable from noise while editing |
| `Process\Thread Count` | whole session | 1–2 (UI thread + the shell's watcher thread pool), never climbing |

## 9. Version compatibility

| # | Steps | Pass criteria |
|---|---|---|
| 9.1 | Premiere 2026 | Treatment `full`; all rows in §3–§5 pass |
| 9.2 | Premiere 2025 | Same as 9.1 |
| 9.3 | Premiere 2024 | Same as 9.1 |
| 9.4 | Unknown build (rename/older version, or set the INI to force the conservative path) | Treatment `reduced`/`conservative`; skin still safe; log explains the fallback |
| 9.5 | Premiere Beta | Treatment notes the Beta channel; no rounding |

---

## Reporting a problem

1. Settings → *Open log file*.
2. Reproduce the problem once.
3. Include in the report: the log, Windows version, Premiere version, display
   configuration (monitors + scaling), theme, whether Performance mode is on, and
   whether the problem survives a toggle.

The log is deliberately quiet: state changes only, repeated lines collapsed to
`… [repeated N times]`. If the log is full of periodic lines, that is itself a bug
and worth reporting.
