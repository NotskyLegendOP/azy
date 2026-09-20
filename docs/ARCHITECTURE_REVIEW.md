# Architecture review

A second-pass engineering review of Azy Skin: is this the right architecture, and
if not, what should replace it?

**Version reviewed:** v1.2.2 (source at the start of this review), fixes released
in **v1.2.3**
**Method:** every source file re-read against the documented behaviour of the
Windows APIs it uses, plus the alternative designs that were considered. Nothing
here was run against Premiere Pro.

Evidence vocabulary, used strictly:

| Term | Meaning |
| --- | --- |
| **VERIFIED** | A machine checked it — a test, a build step, or an inspection that cannot be argued with (a grep for a call that does not exist). |
| **STRONGLY SUPPORTED** | The API documentation and the code agree, and the code does what the documentation says. No runtime observation. |
| **UNVERIFIED** | Requires Windows and Premiere Pro. |
| **HIGH RISK** | An unresolved uncertainty that could invalidate the approach. |


> **Scope note (v1.3.0):** this document is READY FOR REAL-WORLD TESTING (v1.2.3). It covers the static layers
> (frame colours, the ring, the sheet) and is still accurate for them. The duplicate
> window added in v1.3.0 is covered by
> [`AZY_OVERLAY_ARCHITECTURE.md`](AZY_OVERLAY_ARCHITECTURE.md) and
> [`AZY_OVERLAY_TEST_PLAN.md`](AZY_OVERLAY_TEST_PLAN.md), and carries its own claim
> label: IMPLEMENTED — RUNTIME UNVERIFIED.

---

## 1. The question this review had to answer

> If this application were placed on a real Windows machine with the supported
> Premiere Pro version installed, would it behave as intended?

The answer has two halves, and they have very different confidence levels:

- **Is the architecture capable of it?** Yes — *strongly supported*. The design
  uses only mechanisms that do what the code expects of them, and this review
  found no logical impossibility anywhere in it (§4 explains what is and is not
  reachable). Nothing in the implementation depends on a native child control, an
  undocumented hook, or a Premiere API.
- **Will it survive real use?** *Unverified.* No part of the Windows layer has
  ever executed. This review found and fixed seven defects in that layer (§7),
  which is evidence that the layer is where the remaining risk lives.

---

## 2. Premiere integration

| | |
| --- | --- |
| **Current implementation** | `ProcessScanner` (toolhelp snapshot + optional WMI `__InstanceCreationEvent`/`DeletionEvent` subscriptions) finds `adobe premiere pro[ beta\| headless].exe`; `PremiereProbe::find_main_window` picks the process's largest captioned, maximised, visible, non-cloaked top-level window; everything else is geometric. |
| **Problems found** | (a) The window enumerator collected candidates from **every process on the desktop** and filtered by pid afterwards, doing a `DwmGetWindowAttribute` cloak query and two discarded `GetWindowText` calls per window; (b) `GetWindowText` on a foreign window is a blocking cross-process send; (c) the handle chosen could be recycled by another process without Azy noticing. |
| **Alternatives considered** | **Premiere's own extensibility (UXP/CEP)** — forbidden by the brief, and it cannot restyle the host's chrome anyway; a panel is a panel. **Accessibility (UIA) to identify UI regions** — can enumerate the *existence* of some UI elements, but cannot restyle them, adds a COM dependency, and is itself a form of UI automation the brief rules out. **`Accessibility`-driven region detection as a*model* source** — interesting and reusable later, currently unnecessary: the panel map only needs rectangles, and a model already provides them without any dependency. **Window subclassing / hooks** — forbidden and fragile. |
| **Recommended approach** | Keep the process + top-level-window discovery, but scope all per-window work to the target process and never read another process's window text. |
| **Reason** | The only thing Azy needs from Premiere is "which top-level window is the editor", plus its rectangle and DPI. Every other route is more privileged, more fragile, or both. |
| **Risk** | **Low** for the approach, **Medium** for the window-selection heuristic (see §8). |
| **Implemented** | Yes — this review rewrote the enumerator and added ownership validation. |

---

## 3. The overlay architecture (the part most likely to be wrong)

| | |
| --- | --- |
| **Current implementation** | Three independent layers. **Level 1:** documented DWM attributes on Premiere's own frame (`DWMWA_USE_IMMERSIVE_DARK_MODE`, caption/border/text colours, corner preference, optional backdrop). **Level 2:** a four-strip click-through layered ring (`WS_EX_LAYERED` + `UpdateLayeredWindow` with a small premultiplied-ARGB DIB per strip). **Level 3:** one click-through layered window for the whole-window veil, filled with a single colour and blended by `SetLayeredWindowAttributes(LWA_ALPHA)` — no bitmap at all. |
| **Problems found** | None that are architectural. The strip split is deliberate and unit-tested: the horizontal strips own the corner arcs and the vertical strips are inset by the strip thickness, so *no pixel belongs to two strips* (`ring_strip_rects`, asserted by `test_ring_layout`). Without that property the corners would double-blend and read as dark L-shapes. Z-order is explicit and mirror-based (`z_order_anchor` inserts Azy's window between Premiere and whatever is directly in front of it). |
| **Alternatives considered** | **One window for the whole skin** (ring + veil in a single full-window ARGB layer): 8 MB at 1080p, 33 MB at 4K, rebuilt on every change, and DWM blends every pixel of it — for decoration that lives in a 12-pixel band. Rejected. **DirectComposition / Windows.UI.Composition with a transparent top-level window**: GPU-composited and modern, but adds a D3D device, a swap chain per surface and a driver dependency to an application whose GPU budget is deliberately zero, and it does **not** place windows above another process any more reliably than `SetWindowPos` does. Rejected. **`uiaAccess` (UIAccess) in the manifest**: the documented way to let a non-elevated process draw above an elevated one — requires a code-signing certificate and an install under `Program Files`, which this project does not have, and is itself a technique defenders treat as suspicious. Rejected, recorded in `KNOWN_LIMITATIONS.md`. **Owning Azy's surfaces to Premiere's window** (`SetWindowLongPtr(GWLP_HWNDPARENT)`): would make Windows manage z-order and hiding for us — but it writes an ownership relationship into *another process's window*, is visible to that process, and couples Azy's lifetime to a handle it does not control. The explicit ordering Azy already has achieves the same visible result without writing to Premiere's window state. Rejected. **Screenshot-based compositing / backdrop blur**: requires continuous screen capture, which the brief forbids and which the performance budget cannot afford. Rejected; the veil exists precisely so no capture is needed. |
| **Recommended approach** | Keep the three-level design exactly as it is, and keep the strip split. |
| **Reason** | Each level is the cheapest documented mechanism that produces its effect, every level degrades independently, and the total cost is four thin bitmaps (~78 KB at 1080p) plus one constant-alpha window with no bitmap at all. |
| **Risk** | **Medium** — not for the design, but for the one thing that cannot be proven from here: whether DWM actually composites Azy's windows above Premiere's on the user's machine. The round-3 report said it did not. |
| **Implemented** | Already present; reviewed and kept. |

---

## 4. What can actually be reached (the fundamental question)

| Category | What falls in it | How |
| --- | --- | --- |
| **Directly modifiable** | Premiere's non-client frame: caption colour, border colour, caption text colour, dark/light mode, corner preference, and (Windows 11 22H2+, behind the experimental flag) the system backdrop. | `DwmSetWindowAttribute` on the top-level window — documented, reversible, no injection. |
| **Potentially compositable** | Everything inside the window rectangle, because Azy can place its own click-through windows above it: a uniform tint (the veil), the 1 px edge band (the ring), and any region decoration drawn over the modelled panel rectangles. | Layered windows + `UpdateLayeredWindow`. |
| **Detectable but not directly modifiable** | The existence of a top-level frame; its rectangle, DPI, monitor, minimised/maximised/cloaked/foreground state; its owning process and version. | Plain Win32 queries. |
| **Only approximatable** | Where the individual panels are (Project, Timeline, Monitors, docks …). Azy models them from ratios and DIP bands, because the real layout is inside Premiere's process and is not published anywhere Azy is allowed to look. | `panel_map.cpp`. |
| **Impossible without forbidden techniques** | Restyling Premiere's own widgets (buttons, sliders, tabs, menus, tooltips); animating anything Premiere draws; reading the *current* workspace; flattening a panel's contents. | Would need UXP/CEP, injection, or code inside Premiere. Permanently out of scope (`docs/FEASIBILITY.md`, class D). |

Two things this review verified about the last row, because they are the
assumptions most likely to be wrong:

1. **Azy never assumes native child controls exist.** There is no `EnumChildWindows`,
   `FindWindowEx`, `GW_CHILD` (except on Azy's own settings window) or `GetParent`
   anywhere in `src/`. The design needs one top-level window and nothing else —
   **VERIFIED** by inspection.
2. **Azy has no opinion about how Premiere draws its UI.** It never reads a pixel
   except in the user-triggered visibility probe (~40 `GetPixel` samples), never
   captures the screen, and never draws content — only decoration. It is therefore
   not affected by, and cannot be broken by, Premiere's internal rendering model.
   **VERIFIED** by inspection.

---

## 5. "Fake Premiere" check

**No risk.** Azy draws no text, no controls, no icons and no panel content outside
its own debug overlay. There is no timeline, no project panel, no fake toolbar,
no fake menu — nothing that could be mistaken for Premiere's UI. Region treatment,
when it ships, is decoration only: hairlines and a faint falloff over the
modelled rectangles. Azy cannot become a second editor sitting on top of the
first, because it has no mechanism to draw anything that looks like one, and no
interest in doing so. **VERIFIED** by inspection of the renderer (`gdiplus_renderer`
draws rounded-rect stroke paths and nothing else).

---

## 6. Subsystem status

| Subsystem | State | Notes |
| --- | --- | --- |
| Core logic (theme, DPI maths, panel map, settings, versioning, failure tracking) | **Stable** | 578 checks at v1.2.3, 0 failures (649 as of v1.3.0); pure C++ with no Windows dependency. |
| Build, packaging, CI, releases | **Stable** | `verify.sh` 6/6 at v1.2.3 (7/7 as of v1.3.0), artifact inspection + size budget, CI release green for v1.0.0–v1.2.3. |
| Process detection & lifecycle | **Stable** | Event-driven (WMI + toolhelp fallback), now scoped so that idle costs nothing when the observer is live. |
| Window discovery | **Needs Improvement → improved** | The enumerator was desktop-wide and performed a blocked cross-process text query per window. Rewritten in this review. |
| Window tracking | **Needs Improvement → improved** | Geometry/DPI/state refresh was sound; handle-recycling validation was missing and has been added. |
| DWM frame treatment | **Needs Improvement → improved** | Correct save/restore/legacy-attribute handling; now refuses foreign handles and un-applies cleanly when the feature is switched off. |
| Ring rendering & compositing | **Stable (design) / Unverified (result)** | The strip geometry is unit-tested; whether it lands on screen above Premiere is unverified. |
| Veil | **Stable (design) / Unverified (result)** | One colour, one alpha, no bitmap, no per-pixel work. |
| Z-order management | **Unverified** | The mechanism is correct by construction and self-checks after every placement; it has never been observed working. |
| Input transparency & focus | **Strongly supported** | All four extended styles are required before a surface is shown, verified on the live window; no activation call exists anywhere in `src/`. |
| Event system | **Stable** | Two process-wide hooks + one cloak hook, registered once, unhooked on stop, `WINEVENT_SKIPOWNPROCESS` so Azy cannot feed itself. |
| Performance / idle cost | **Needs Improvement → improved** | Two idle-cost defects found and fixed (a scan storm and a dead 1 Hz desktop enumeration). |
| Resource lifecycle | **Needs Improvement → improved** | A GDI leak (one device context + one DIB per resize) was found and fixed. |
| Settings UI | **Stable** | Dark window, DPI rebuild destroys children correctly, every control has a consumer. |
| Visual system | **Unverified** | Colour maths is unit-tested; the result has never been looked at. |
| Installer / distribution | **Unverified** | Script reviewed line by line and built by CI; never installed by a human. |

---

## 7. What this review changed (and what it refused to change)

**Changed** — all verified by rebuild + the native test suite in the same commit:

1. **Window discovery is now pid-scoped** and no longer reads another process's
   window text (`premiere_probe.cpp`). This removes a cross-process blocking call
   from the per-window path and turns per-desktop-window work into
   per-Premiere-window work.
2. **The idle scan storm is gone**: rescanning on desktop-wide window activity is
   now gated by the detector (`wants_window_activity()`), and the fallback
   interval when no target exists and no observer is available is 1 s instead of
   0.2 s. The WMI path still delivers start/stop immediately.
3. **Handle recycling is now checked** before anything acts on a stored `HWND`
   (`window_belongs_to`), in both the tracker and the DWM composer.
4. **A GDI leak was fixed** in `GdiPlusRenderer::release()`: the DIB was never
   deselected before `DeleteDC`/`DeleteObject`, so both handles leaked on every
   resize, DPI change and monitor move (`previous_bitmap_`).
5. **The dead 1 Hz window count was deleted.** It enumerated every top-level
   window on the desktop once a second — including a DWM cloak query per visible
   window — and the result had no reader anywhere in the program.
6. **The DWM dark frame is now un-applied** when the feature is switched off while
   attached (performance mode, Safe Mode, a per-feature override).
7. **Out-of-scope buffer bug fixed**: `read_file_version` used a `wchar_t` buffer
   that had gone out of scope (dangling pointer in the version-string fallback).
8. **Dead code removed**: `find_top_level_windows`, `window_text`,
   `SkinTarget::window_count`, and the leftover `tools/.tmp_*.py` scripts from
   earlier work.

**Refused to change, with reasons** — the point of a review is not to rewrite
everything:

- The four-strip ring (correct, cheap, unit-tested; one window would cost 100–400×
  the bitmap memory).
- The constant-alpha veil (the cheapest possible whole-window tint; an ARGB veil
  would be the single most expensive thing in the program).
- Explicit z-order placement instead of ownership tricks or topmost windows
  (§3 lists why each alternative lost).
- The panel map as a *model* (approximation is the only honest option; §4).
- The DWM attributes as the primary treatment (they are the one documented way to
  change Premiere's own frame, and they revert cleanly).

---

## 8. Known weak points that remain

1. **The main-window heuristic is a heuristic.** "Largest maximised captioned
   top-level window of the process" is right for a normal Premiere session, and
   the code logs which window it chose, but a build that opens a bigger framed
   window than the editor would be tracked instead. There is no way to ask
   Premiere, so the mitigation is the log line plus the fact that the choice is
   re-evaluated whenever the window set changes. Risk: **Medium**, unverified.
2. **The veil and Premiere's video path.** A constant-alpha window over the whole
   frame can, in principle, push Premiere's video out of a hardware overlay plane
   and make DWM composite it instead — the classic cause of "playback got heavy
   after I installed that overlay". Unverifiable here; the mitigations already
   exist (the overlay strength slider, the per-feature override, and performance
   mode). Risk: **HIGH RISK until measured on the target machine**.
3. **Elevated Premiere.** UIPI makes a lower-integrity overlay impossible. Detected
   and reported, with a one-click restart-as-administrator offered; not fixable
   otherwise without signing for UIAccess. Risk: **known and documented**.
4. **Customised workspaces** will not match the panel model, and Azy cannot detect
   them. Mitigation: the manual UI-profile selector. Risk: **known, permanent**.
5. **Region treatment is not shipped yet.** The map is a model and a debug tool
   today; the brief's "skinned panels" reading needs the P5 items.

---

## 9. Verdict

| | |
| --- | --- |
| Subsystems **Stable** | Core logic, build/release, detection & lifecycle, event system, settings UI, ring/veil design |
| Subsystems **Needs Improvement** | Window discovery and tracking (improved here, still heuristic), performance/idle cost (improved here) |
| Subsystems **High Risk** | Veil vs. video playback; z-order/visibility on the user's machine (round-3 report unresolved) |
| Subsystems **Unverified** | Everything that draws, everything that touches Premiere's window, performance, installer |
| **Overall engineering status** | **READY FOR REAL-WORLD TESTING** |

The architecture is the right one for the constraints: three independent,
cheap, documented layers, no injection, no capture, no polling, no fake UI, and
every risky technique either avoided or documented. It is not production-
candidate material, and the reason has nothing to do with the code: nothing in
the Windows layer has ever been observed running next to Premiere Pro, and the
last observation the user gave was that the skin was not visible.
