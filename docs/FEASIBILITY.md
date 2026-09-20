# Azy Skin — technical feasibility report

This is the Phase 1 deliverable of the implementation specification: what a
separate process can and cannot do to the way Premiere Pro looks, decided
*before* the visual work, with the technique named for every row. It is the
answer to "inspect the actual Premiere window first, do not assume that Premiere
allows arbitrary native UI skinning".

Three closely related documents carry the detail:

* [`TECHNIQUES.md`](TECHNIQUES.md) — every Windows API Azy uses, judged against
  six safety tests, plus everything that was considered and rejected, with the
  reason.
* [`LIMITATIONS.md`](LIMITATIONS.md) — the resulting feature boundary, in user
  terms.
* [`COMPATIBILITY.md`](COMPATIBILITY.md) — the version profiles and the fallback
  ladder.

---

## 1. What Premiere's UI actually is

| # | Inspection target | What it is on Windows | Consequence |
|---|---|---|---|
| 1 | Main window structure | **One** top-level window (`Premiere Pro`, class `Premiere Pro`), a normal `WS_OVERLAPPEDWINDOW` | Its non-client area is ordinary Windows furniture: Azy can restyle it (DWM) |
| 2 | Child windows | Very few. The client area is *not* a tree of controls | There is no per-panel `HWND` to paint, move or restyle |
| 3 | Panel hierarchy | Drawn by Premiere's own toolkit inside the single client area (it is a DirectX-composited editor, not a dialog) | Panels are **pixels**, not windows |
| 4 | Window handles | One handle for the main window, one per *undocked* panel, one per modal dialog, one per open menu/popup | Three of those four are addressable; the docked grid is not |
| 5 | Accessibility / UI Automation | Adobe ships partial UIA support: some panels and toolbar items publish names and bounding rectangles, coverage varies by version and is not guaranteed for a given panel | Useful as **one optional source of rectangles**, never as the foundation |
| 6 | Native controls | None that belong to Premiere (the menus/toolbars are Premiere's own drawing, not `BUTTON`/`TOOLBARCLASSNAME`) | `WM_SETFONT`/owner-draw tricks do not apply |
| 7 | Rendering surfaces | Premiere composites into its client area itself | Anything Azy draws that must interact with Premiere's content would have to be *around* it, not *inside* it |
| 8 | Docked panels | Regions of the client area | Rectangles can be *modelled* (see §3), never restyled individually |
| 9 | Undocked panels | Real top-level windows | **Restyleable** — dark frame + ring, exactly like the main window |
| 10 | Menus (menu bar) | Native menu attached to the main window, drawn by Windows with the system's light theme | Not themable by a third party; the frame it opens from can be dark |
| 11 | Dialogs | Real top-level windows owned by Premiere | **Restyleable** — dark title bar + subtle border |
| 12 | Timeline | A region of the client area | Region treatment only (background wash, separators, edge accents) |
| 13–15 | Program Monitor / Source Monitor / Project panel | Regions of the client area | Region treatment only; video pixels are never touched by Azy |
| 16 | Effect Controls | Region of the client area; keyframe and parameter widgets are Premiere's own drawing | Region treatment only |
| 17 | Lumetri Color | Region of the client area | Region treatment only |
| 18 | Audio meters | Region of the client area, continuously repainted by Premiere | Region treatment only, and any Azy content over it must be static (a moving layer over a moving meter is the one place where Azy could cost frames) |
| 19 | Toolbars | Region of the client area | Region treatment only |

**The single fact everything follows from:** docked panels are not windows. A
process that is not Premiere can place things *above* Premiere's pixels or
*around* its window — it cannot paint *inside* Premiere's panel tree.

## 2. What that leaves, and what it costs

Four classes, used consistently in `docs/PROGRESS.md`:

| Class | Meaning | Techniques |
|---|---|---|
| **A — done** | Shipped and verified | DWM frame attributes, click-through layered surfaces, static overlays |
| **B — possible, planned** | Native and safe, not written yet | Region chrome (hairlines, bands, washes) clipped to modelled panel rectangles; dark frame + ring for undocked panels and dialogs; accent + preset system; debug/profile tooling |
| **C — approximation only** | The *effect* is reachable, the *widget* is not | "Glass panels", "panel states", "buttons/dropdowns/menus", "timeline clips", "playhead", "audio meter glow", "spacing", "typography" |
| **D — not possible, and not attempted** | Requires injecting, hooking or patching Premiere | Recolouring Premiere's panels or widgets, per-clip and per-playhead styling, blurring the desktop behind Premiere's panels, animating Premiere's own UI, replacing its menus |

Class C is the honest heart of this project: a panel cannot be given a glass
surface *inside itself*, so the glass is expressed where a separate process can
legitimately put it — as a static, click-through treatment of the band around and
between panels (edge falloff, a 1px separator where two docks meet, a subtle top
highlight under the application header). Viewed at normal size this reads as
panels gaining depth and separation; it does not pretend to be a repaint of
Premiere's widgets, and it cannot break one.

Class D is not a schedule problem. It is the definition of the project: no
injection, no hooking, no patching, no files touched inside an Adobe directory.
Some rows in the specification therefore have two answers — what was asked for,
and what will actually be built. They are listed side by side in
`docs/PROGRESS.md` so nothing is quietly dropped.

## 3. Panel discovery: how Azy models a layout it cannot read

Because panels are pixels, Azy never assumes a fixed grid. It builds a
**panel map** from three inputs, in this order:

1. **Measured** — the tracked window's client rectangle, DPI, monitor work area
   (already exact, event-driven).
2. **Published** — UI Automation bounding rectangles *when Adobe publishes them*
   for the running version. Optional: absent rectangles are not an error, and
   their absence never leaves a gap in the treatment.
3. **Modelled** — a UI profile per Premiere major version (2024 / 2025 / 2026 /
   unknown) plus a workspace profile (`Editing`, `Color`, `Audio`, `Effects`,
   `Graphics`, custom), holding ratios rather than pixel coordinates: where the
   header band ends, the horizontal and vertical dock splits, the timeline band.
   Ratios scale with the client rectangle and the DPI, so resizing, maximising,
   monitor changes and 100–200% scaling all fall out of the same numbers.

The workspace is identified from what Premiere publishes (panel names, caption
text) with a fallback to the profile's default. Anything the model cannot place
is simply **not decorated** — an unplaceable region costs a missing hairline,
never a broken layout, and it is logged once.

This is the spec's "version → UI profile → panel identification → skin
configuration" chain, and it is why a future Premiere redesign is a config
change rather than a rewrite. Phase 1 §41 (debug mode) is what makes it
maintainable: a debug overlay that draws the rectangles Azy believes in, so a
screenshot from a user is enough to correct a profile.

## 4. Rendering feasibility

| Requirement | Verdict |
|---|---|
| GPU-accelerated translucent surfaces | **Yes**, via the desktop window manager: `UpdateLayeredWindow` surfaces and constant-alpha layered windows are composited by DWM (GPU), not by Azy's CPU |
| DirectComposition / Direct2D / D3D | **Not needed** and not used. They buy nothing here: every surface Azy draws is static between events, and GDI+ produces the whole ring in well under a millisecond per change |
| Background blur behind a panel | **No.** Windows offers blur only for a process's *own* window (`DWMWA_SYSTEMBACKDROP_TYPE` on Windows 11, 1.4 in `TECHNIQUES.md`), not for a foreign window's client area. Blurring Premiere's pixels means reading and re-compositing them, i.e. constant screen capture — explicitly ruled out |
| Dirty-region rendering | **Yes**: each surface is repainted only when its key (geometry, colour, thickness) changes |
| Reusing rendered materials | **Yes**: one palette per settings change; two DIB sections total (four strips + one veil), reused for the lifetime of the attachment |
| Minimal polling | **Already true**: process/window `WinEvent` hooks, shell notifications and timers that are armed only while something is unsettled |

## 5. Input feasibility

The rule is that Premiere keeps every input. Azy's layers are created with
`WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, never shown in
Alt+Tab, never activated, and are moved with `SWP_NOACTIVATE`; the settings
window restores the previous foreground window when it closes. Click-through was
verified by construction and is re-verified on every probe; a surface that cannot
be placed above Premiere is hidden rather than left to swallow clicks. Nothing
Azy does installs a keyboard hook, a mouse hook or a raw-input subscription —
the acceptance list in §45 (`Space`, `C`, `V`, `Ctrl+Z`, `Ctrl+S`, `J/K/L`) is
unaffected by construction.

## 6. Two conflicts in the specification, decided explicitly

1. **Animation (§28) vs the project's standing rule (no animations of any
   kind).** The original brief forbids animation outright; this specification
   lists 80–180 ms transitions. Only Azy's *own* layers could animate at all
   (Premiere's widgets are unreachable), and those are static by design, so the
   practical difference is small. Azy stays static by default; an
   `animations = true` switch will be implemented for its own surfaces and stay
   **off** until the user asks for it. That keeps both instructions satisfiable
   and costs nothing.
2. **"Do not use a single giant overlay" (§5) vs "cover the whole window like an
   overlay" (round-3 request in 1.1.0).** The overlay that ships is the cheapest
   possible reading of both: one click-through, non-activating, constant-alpha
   layer per tracked window — no bitmap, no blur, no input, hidden with the
   skin, and switchable off in Settings. Everything Azy does *in addition* is
   region-based, per §5's "only render where necessary".

## 7. What is required from the target machine

Three things cannot be decided from this repository and are needed before the
region work can be tuned:

1. **The reference image.** It did not arrive with the specification; anything
   claiming to match its palette without it is guesswork. Attach it and the
   accent/glass/darkness numbers are read off it directly.
2. **One screenshot of a debug run** at the user's normal window size and DPI,
   with the debug overlay on. That single image settles the panel model for
   their workspace.
3. **A confirmation run** of the acceptance rows that need a live Premiere
   (playback, scrub, dock/undock, export, import, save).

## 8. Verdict

The specification is buildable as written in the areas that do not require
touching Premiere's process, provided the class-C rows are understood as *the
closest possible visual treatment* rather than a repaint of Premiere's widgets,
and provided the class-D rows are understood as permanently out of scope. The
remaining work, its order and its cost are tracked in
[`PROGRESS.md`](PROGRESS.md) / [`progress.html`](progress.html).
