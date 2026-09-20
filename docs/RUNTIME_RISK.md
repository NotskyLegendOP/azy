# Runtime risk

Everything that cannot be settled without a Windows machine running Premiere Pro,
what the code is expected to do, and how each item can be confirmed or killed in
a single session.

Written after the deep review; fixes released in **v1.2.3**.

Terms: **VERIFIED** (a machine checked it) · **STRONGLY SUPPORTED** (documentation
+ code evidence, no runtime) · **UNVERIFIED** (needs the real environment) ·
**HIGH RISK** (an unresolved uncertainty that could invalidate the approach).


> **Scope note (v1.3.0):** this document is the v1.2.3 runtime risk list. It covers the static layers
> (frame colours, the ring, the sheet) and is still accurate for them. The duplicate
> window added in v1.3.0 is covered by
> [`AZY_OVERLAY_ARCHITECTURE.md`](AZY_OVERLAY_ARCHITECTURE.md) and
> [`AZY_OVERLAY_TEST_PLAN.md`](AZY_OVERLAY_TEST_PLAN.md), and carries its own claim
> label: IMPLEMENTED — RUNTIME UNVERIFIED.

---

## 1. The theoretical runtime simulation

Each scenario is traced through the actual code paths. "What the code does" is
what the source says will happen — **not** an observation.

### Scenario 1 — Azy starts, Premiere is closed

| | |
| --- | --- |
| What the code does | `PremiereDetector::start` subscribes to WMI and forces one scan; no Premiere process is found, so no target is set. The performance manager reports `NoWindow`, the skin stays suspended, and no surface is ever created (the windows are created lazily on first present). The 1 Hz safety net ticks: it reads settings, evaluates the decision, builds a status string and returns — no scan, no drawing. |
| Expected | Waits without wasting resources. |
| Status | **STRONGLY SUPPORTED.** After this review the idle path also stops rescanning the process list on unrelated desktop window activity (`DB-2`), which was the one place a resource could be wasted here. Actual CPU use: **UNVERIFIED**. |

### Scenario 2 — Azy starts, Premiere is already open

| | |
| --- | --- |
| What the code does | The forced scan finds the process, reads its version resource, picks the main window, publishes `Started`, and the controller binds the tracker, starts watching the process's thread, and applies the skin in the same turn. |
| Expected | Detects and attaches. |
| Status | **STRONGLY SUPPORTED** for detection and binding; **UNVERIFIED** for whether the result is visible. |

### Scenario 3 — Premiere starts after Azy

| | |
| --- | --- |
| What the code does | WMI reports the process creation (the provider polls at `WITHIN 1`, so up to ~1 s), which forces an immediate scan. If the editor window does not exist yet, the detector keeps the target and remembers "window missing"; window activity then schedules further scans at 0.2 s until the window appears, at which point it publishes `Changed` and the skin applies. Without WMI, the same path runs on a 1 s cadence. |
| Expected | Detects it, attaches as soon as the window exists. |
| Status | **STRONGLY SUPPORTED** for the state machine. The 1 s worst case without WMI is a deliberate trade for CPU and is documented. |

### Scenario 4 — Premiere closes

| | |
| --- | --- |
| What the code does | The deletion event forces a scan, no candidate process remains, `Stopped` is published: DWM attributes are restored, the ring/veil/debug windows are hidden *and destroyed*, the panel map is cleared, the tracker is cleared, the watched pid/thread are reset, the failure state is persisted. |
| Expected | Detaches cleanly, no residual handles or CPU. |
| Status | **STRONGLY SUPPORTED** for the sequence. Whether it leaves *zero* GDI/USER handles behind is **UNVERIFIED** — one leak that would have made this false (a device context and a DIB per resize) was found and fixed in this review (`DB-3`). |

### Scenario 5 — Premiere restarts

| | |
| --- | --- |
| What the code does | Same path as scenarios 3 and 4 in sequence. A different pid is a new target: the previous surface windows are reused (they were destroyed on stop and are recreated on attach), the tracker is re-bound, and the new process's thread id becomes the event filter's key. |
| Expected | Reconnects without accumulating anything. |
| Status | **STRONGLY SUPPORTED.** This is the scenario the review specifically hunted for accumulation in: surfaces are destroyed rather than hidden on stop, the detector's WMI sinks are released with the subscription, and the renderer's bitmaps are freed with the strips. |

### Scenario 6 — Premiere moves to another monitor

| | |
| --- | --- |
| What the code does | Location events mark the geometry dirty; the tracker re-reads the frame, the monitor, the work area and the DPI from the window itself (never from the primary monitor); if the DPI or the rectangle changed, the visual key changes and the surfaces are re-placed and re-rendered at the new size. |
| Expected | The skin follows, at the new monitor's scale. |
| Status | **UNVERIFIED** — this is the highest-value thing to watch during a real session (see §4), because coordinate mistakes here are exactly what the earlier audit found twice. |

### Scenario 7 — DPI changes (150 % → 175 %, or a monitor with different scaling)

| | |
| --- | --- |
| What the code does | `dpi_for_window` is read per refresh, the visual key carries the DPI, and every DIP value goes through the same conversion helper (`dip_to_px`/`MulDiv`) — there is no scattered scaling factor anywhere in `src/`. The ring's strips are sized in **physical pixels** and drawn 1:1, so a 1 px line stays 1 px. The debug overlay's font and row spacing are rebuilt for the new DPI. |
| Expected | No alignment problems. |
| Status | **STRONGLY SUPPORTED** for the arithmetic (unit-tested at 100/125/150/175/200 %); **UNVERIFIED** visually. A grep for hard-coded scale factors (`* 1.25`, `/ 1.5`, `96.0`) outside the core returns nothing. |

### Scenario 8 — The user changes workspace (Editing → Color → Audio)

| | |
| --- | --- |
| What the code does | Panel docking inside Premiere generates window activity, so a sync runs and the panel map is rebuilt from the (unchanged) client rectangle. The **map's layout ratios do not change**, because Azy cannot see which workspace is active — it uses the manual *UI profile* setting, which defaults to Editing's ratios. |
| Expected | "Azy re-discovers/recalculates the relevant regions." |
| Status | **PARTIAL by design, and honest about it.** Azy recomputes geometry from the frame on every apply, so a resize or DPI change is free; it does **not** detect the workspace itself. That is why the manual UI-profile selector exists. Today this only affects the debug overlay, because no region treatment ships yet. |

### Scenario 9 — Maximise / restore

| | |
| --- | --- |
| What the code does | State change → tracker marks the target maximised; `ring_frame()` replaces the reported rectangle with the monitor's work area, because a maximised window's reported bounds hang off the display by the resize border; the surfaces are re-placed and re-rendered. On restore, the reported bounds are used again. |
| Expected | The skin follows, and stays inside the display. |
| Status | **STRONGLY SUPPORTED.** This is the case the user reported broken in round 3 ("maximized" confirmed via the questions), and the code now contains an explicit, logged work-area substitution for it. **UNVERIFIED** whether it is actually visible on the user's machine — this is the single most important thing to re-test. |

### Scenario 10 — The user scrubs the timeline / plays back

| | |
| --- | --- |
| What the code does | Nothing. There is no input hook, no timer tied to playback, no capture, and no per-frame work. The pointer events go to Premiere because every Azy window is `WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW` and answers `WM_NCHITTEST` with `HTTRANSPARENT`. |
| Expected | Azy does not interfere. |
| Status | **STRONGLY SUPPORTED** for input (no input path exists to interfere with; verified by grep for `SendInput`, `SetWindowsHookEx`, `AttachThreadInput` — all absent). **HIGH RISK** for the one indirect effect that *can* touch playback: a constant-alpha window over the whole frame can prevent Premiere's video from reaching a hardware overlay plane, in which case DWM composites it instead. See §3, item R1. |

### Scenario 11 — The user opens a context menu or a modal dialog

| | |
| --- | --- |
| What the code does | Nothing special. Azy's surfaces are ordinary windows placed directly above Premiere's frame and below whatever was in front of it; a dialog created afterwards is created above them. Nothing in Azy takes activation, so the menu/dialog keeps focus and keyboard routing. |
| Expected | Azy does not block it. |
| Status | **STRONGLY SUPPORTED.** One consequence worth stating: while a Premiere dialog is open **the veil is behind it**, so the dialog is not tinted. That is intended (the dialog is Premiere presenting information the user must read), but it means the skin looks "partial" during a modal dialog. |

### Scenario 12 — The user is editing with keyboard shortcuts

| | |
| --- | --- |
| What the code does | Nothing. Azy installs no keyboard or mouse hooks (the only hooks are `SetWinEventHook` *observation* hooks for process/window events, which do not intercept input), never activates a window, and is not in the Alt+Tab list. |
| Expected | Premiere receives everything normally. |
| Status | **STRONGLY SUPPORTED** and effectively VERIFIED by construction: there is no code path that can consume or delay a key press. |

---

## 2. What this environment cannot verify — the complete list

Nothing below has been observed. All of it is UNVERIFIED.

| Area | Why it cannot be checked here |
| --- | --- |
| **UI composition** — whether the ring, veil and debug overlay are actually composited above Premiere | Requires DWM, a real window and Premiere. The code self-checks the z-order and the presence of drawn pixels, but that check runs on the target machine, not here. |
| **HWND discovery** — whether the chosen window really is the editor frame | Requires a running Premiere to enumerate. The heuristic and its log line can be reviewed; its answer cannot. |
| **Input behaviour** — click-through, focus, Alt+Tab exclusion at runtime | The styles are verified on the live window by `input_guard::verify`, which cannot run without Windows. |
| **Overlay positioning** — pixel-exact alignment at 100–200 % DPI on one and several monitors | Needs a display. |
| **GPU composition** — whether layered windows cost anything measurable during playback | Needs a GPU, DWM and Premiere playback. |
| **Preview/playback interaction** — hardware overlay planes (risk R1) | Same. |
| **Performance** — 0–1 % idle, < 2 % monitoring, < 100 MB RAM | Needs a stopwatch and Task Manager on the target machine. The design has no mechanism that could plausibly exceed the budget, which is not the same as a measurement. |
| **Rendering correctness** — that the ring looks like a 1 px hairline and a subtle bezel rather than a drawn frame | Needs eyes. |
| **Visual quality** — dark, glossy, glassy, premium, subtle | Needs eyes and a reference image. |
| **Installer** — install → run → uninstall round trip | Inno Setup cannot run here; CI compiles the installer but nobody has clicked it. |
| **Long sessions** — handle and memory growth over hours | The one leak of that class found by inspection (R2 in the previous version of this list) is fixed; whether any other exists can only be seen in Task Manager over time. |
| **Premiere version differences** — 2024 / 2025 / 2026 / Beta | No Premiere of any version is installed here. |

---

## 3. The risk register

| # | Risk | Why it is a risk | What would settle it | Severity |
| --- | --- | --- | --- | --- |
| **R1** | A constant-alpha window over the whole frame pushes Premiere's video out of a hardware overlay plane. | The Program Monitor may be composited by DWM instead of scanned out directly. This is the classic "playback got heavier after I installed an overlay" effect and it is invisible in any inspection of the code. | Watch playback CPU/GPU with the veil on and off (`Appearance → Overlay`). If it matters, the fix is already available to the user (lower the strength or turn the overlay off) and the proper fix is region-based: exclude the Program Monitor rectangle, which the panel map already models. | **HIGH RISK** |
| **R2** | The user's round-3 report — the skin was not in front of Premiere — has never been re-tested. | It was attributed to z-order/UIPI and answered with explicit ordering plus a diagnostic, but no observation has confirmed it on the machine where it failed. | One screenshot with debug mode on, plus the log line `ring: … above Premiere: yes`. | **HIGH RISK** |
| **R3** | The main-window heuristic could pick the wrong window. | "Largest maximised captioned top-level window of the process" is right for a normal session; a build that opens a bigger framed window first would be tracked instead. Azy cannot ask Premiere. | The `window tracker: target 0x…` log line names the window; compare it with the window the user is editing in. | Medium |
| **R4** | Elevation mismatch (Premiere as administrator, Azy not). | UIPI makes the overlay impossible; every call still succeeds, so it fails silently except for the log and the tray warning. | Run Azy elevated as well and compare. Azy offers this in one click. | Medium, detected |
| **R5** | Region treatment will be approximate on a customised workspace. | The model uses published default ratios; Azy cannot read the real layout. | Compare the debug overlay's rectangles against a real screenshot of Premiere. | Medium, permanent |
| **R6** | Safe Mode can look like a regression. | After repeated failures the skin stays off until the user opts back in through Settings → Advanced. | Read the log line that names the reason. | Low, by design |
| **R7** | The veil dims a dialog-less window uniformly, including the Program Monitor. | Uniform tint is what round 3 asked for; there is no region falloff yet. | Look at it; the strength slider is the control. | Low, by design |

---

## 4. How to close most of this list in one session

Roughly ten minutes, on the machine that has Premiere:

1. Install v1.3.0 (or run the portable build). Open Premiere, **maximise** it.
2. Settings → **Check visibility**. The window reports whether the overlay and the
   ring reached the screen, in plain words. If it says "NOT on screen", stop here
   and send that line — it is the whole diagnosis.
3. Settings → Advanced → **Debug mode**, then take **one screenshot**. That single
   image shows the tracked window, its class, the client rectangle, the DPI, the
   monitor, and every modelled panel rectangle drawn over the live window.
4. Drag Premiere to a second monitor (or change display scaling) and watch whether
   the ring stays on the window edge.
5. Press `\` for fullscreen playback, then restore; maximise, then restore.
6. Play a clip for a minute with the overlay at its default, then with it set to
   0 %, and compare CPU in Task Manager (risk R1).
7. `%LOCALAPPDATA%\Azy Skin\azy.log` will contain, per geometry change, the
   rectangle it drew on and whether the ring ended up above Premiere. Two lines
   from it are worth more than any amount of further inspection here:
   `window frame: …` and `ring: … above Premiere: yes/no`.

---

## 5. Status

| | |
| --- | --- |
| Verified without a runtime | Build, versioning, release pipeline, 578 logic checks at v1.2.3 (649 as of v1.3.0), absence of injection/input/hook/capture techniques, GDI/DWM resource discipline, the strip geometry contract |
| Strongly supported, no runtime | Detection and lifecycle state machine, event wiring, click-through contract, DPI arithmetic, idle-cost design |
| Unverified | Everything that draws or runs next to Premiere |
| High risk | R1 (playback/overlay-plane interaction), R2 (visibility on the user's machine, unresolved since round 3) |
| **Overall engineering status** | **READY FOR REAL-WORLD TESTING** |

Not "READY FOR LIMITED TESTING" and not "PRODUCTION CANDIDATE", because no
observation exists that the skin is visible on the machine that reported it
missing. The code is in the best state it can reach without that observation; the
next useful action is not another review, it is one run.
