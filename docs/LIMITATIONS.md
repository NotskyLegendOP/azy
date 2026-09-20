# Azy Skin — What it cannot do, and why that is deliberate

The design brief contains a rule that shapes this entire project:

> Do not assume that Windows provides a universal API for changing the internal
> appearance of another application's controls. Premiere renders much of its
> interface itself. […] If a specific native Premiere element cannot be safely
> restyled externally, skip it.

This document is the honest answer to "so why doesn't Azy recolour the panels
too?".

---

## Why Premiere's internal UI cannot be restyled from outside

Premiere Pro draws its interface itself. Its panels, panel headers, tabs,
buttons, scroll areas, dropdowns, context menus, dialogs and timeline are not
Windows controls — there are no `HWND`s for them to style, no window classes to
theme, and no `WM_THEMECHANGED` to respond to. They are pixels drawn by
Premiere's own compositor inside a small number of large child windows.

That leaves exactly three ways an external process could change them:

1. **Get code inside Premiere** (inject a DLL, hook its renderer, patch its
   memory). This is precisely what the brief forbids, and rightly so: it risks
   Premiere's stability and its data, breaks on every update, can be flagged as
   tampering, and is impossible to make reliably reversible.
2. **Modify Premiere's files** (theme resources, DLLs, the executable). Forbidden
   for the same reasons plus one more: Adobe support, digital signatures and
   future updates all break, and uninstalling could not restore certainty.
3. **Draw on top of them** — cover Premiere's panels with Azy's own pixels.

Option 3 sounds reasonable until you look at what "draw on top" requires in
practice:

* Premiere's panel layout is **internal state**. Panel positions, splits, tab
  order, docked/floating state, collapsed headers, scroll positions and
  zoom levels live inside Premiere and are not exposed through any documented
  interface. To place a decoration over each panel header, Azy would have to
  *infer* the layout from pixel content or from an undocumented window hierarchy
  — and re-infer it on every docking change, layout preset switch, workspace
  switch and zoom.
* Any mistake is not cosmetic. A surface placed where Azy *thinks* a panel header
  is, while Premiere has moved it, is a hairline drawn across the timeline — or,
  worse, a region that intercepts a click if it is anything other than perfectly
  click-through and perfectly positioned.
* The cost is not bounded. Following the layout means continuously enumerating and
  re-reading geometry — the exact "constant screen capture / continuous rendering"
  pattern the performance requirements forbid.
* The benefit is marginal. Premiere's own dark themes are already competent; the
  perceived "polish" difference comes almost entirely from the *frame*, the *panel
  separation*, the *borders* and the *darkness* — which is what Azy does.

So Azy stops at the boundary of what Windows officially supports for another
process's window and spends its complexity budget there instead.

---

## The resulting feature boundary

| Region | Restyled by Azy? | Notes |
|---|---|---|
| Window title bar / caption | ✅ Yes | Dark mode + charcoal colours (`DWMWA_*`), Windows 11 |
| Window frame / outer border | ✅ Yes | Frame border colour, plus Azy's own 1px hairline ring drawn inside the visible frame |
| Window corners | ✅ Yes, where Windows supports it | Windows 11 rounded frames; Azy's ring mirrors the same 8 DIP radius so the two agree |
| Panel separation look | ⚠️ Partly, indirectly | The dark frame, the restrained panel wash at the window edge and the absence of bright chrome change how panel boundaries *read*; individual panel divider lines are Premiere's own drawing and are not modified |
| Panel headers, tabs, toolbars | ❌ No | Drawn by Premiere; no external API exists. Left untouched by design |
| Menus, context menus, dialogs | ❌ No | Premiere's own rendering. Azy's own settings window is dark-themed, but Azy never draws into Premiere's |
| Buttons, input fields, dropdowns, scroll areas | ❌ No | Same reason. Never forced |
| Timeline, Project panel, monitors | ❌ No | Untouched, unmodified, un-overlaid. This is the "do not redesign Premiere" rule |
| Premiere's preferences, projects, caches, plugins | ❌ No | Azy never reads or writes any of them |

Premiere's window stays Premiere's window: its client area, its hit testing, its
message loop, its focus behaviour, its drag-and-drop, its shortcuts and its
rendering are all exactly what Adobe shipped.

---

## Other boundaries worth stating plainly

* **Azy cannot make Premiere itself see-through.** Making Premiere's client area
  translucent would require either `WS_EX_LAYERED` on Premiere's own window (which
  changes its painting and hit-testing semantics — a behaviour change, not a visual
  one) or capturing and re-compositing its output (expensive, wrong on HDR/10-bit
  displays, and content-protected in some configurations). What Azy does instead is
  *cover* the window with its own constant-alpha layer (the overlay): the desktop
  never shows through, Premiere is never modified, and the whole window still reads
  as one darker, framed surface.
* **The overlay dims everything, including the video.** It is one solid translucent
  layer over the entire window, so the Program and Source monitors are ~30% darker
  at the default strength. That is the honest cost of "the skin covers the whole
  window": the alternative would be to find and skip Premiere's video rectangles,
  which means reading Premiere's internal layout, which Azy does not do. Turn
  *Cover the whole window* off, or pull *Overlay strength* down, when you grade
  footage — the edge treatment alone never touches the picture.
* **Azy cannot follow Premiere's internal docking.** If the user undocks a panel
  into a floating window, Azy does not decorate that floating window. It remains a
  normal Premiere window; the main window's treatment is unaffected. Decorating
  floating panels would require consistent per-window identification that Windows
  does not provide for child windows of a foreign process.
* **Azy cannot theme Premiere's own splash screen**, which appears before the
  editor window exists — and it should not try; Azy waits for the real window.
* **Azy is not a substitute for Premiere's dark theme.** It can darken and frame
  the whole window (the overlay) and give its edge depth and separation, but it does
  not repaint Premiere's panels, timeline, buttons or icons — those are drawn by
  Premiere from its own theme, and changing them means changing Premiere.
* **On Windows 10, the frame stays square and uncoloured.** Windows simply does
  not offer those attributes for another process's window on that OS. Azy reports
  this honestly in Settings ("Treatment: reduced") rather than faking it.

---

## What happens when Azy meets something it does not understand

1. It logs one line (not a stream).
2. It falls back to the next safest treatment.
3. It never escalates to a riskier technique.
4. After three failures in five minutes it enters Safe Mode, tells the user in
   plain language, and waits to be re-enabled manually.

The most invasive thing Azy does in response to difficulty is *less*.

## Premiere running as administrator

Windows does not let a process place its windows above the windows of a process
with a higher integrity level (User Interface Privilege Isolation). If Premiere Pro
is started with "Run as administrator" and Azy Skin is not, Azy's ring is composed
*behind* the Premiere window: every call succeeds and nothing is ever visible.
Azy compares its own integrity level with Premiere's when it attaches, logs a
warning, says so in the settings window, and shows a one-time tray notification.
The two ways out are the same two Windows offers: run Azy Skin as administrator as
well, or run Premiere Pro normally (which is what Adobe supports).

There is no way around this restriction that Azy would accept: bypassing it means
injecting code or privileges into another process, which is exactly the kind of
technique this project rules out.

## If Azy is force-killed (Task Manager, a crash)

Premiere is unaffected: it keeps running, keeps focus, and never noticed Azy. Two
details are worth knowing:

* **No ghost surface.** The composition strips belong to Azy's process, so Windows
  destroys them with it. Nothing translucent is left floating over the desktop.
* **The frame treatment stays until Premiere restarts.** DWM attributes live on the
  *window*, not in Azy: a dark title bar and an extended frame remain dark after a
  forced kill. That is not damage - the window still behaves exactly as before, and
  the next Azy start (or a Premiere restart) resets it. It is the honest cost of
  using only documented, non-invasive window attributes, and the same reason Azy
  never leaves hooks or patched code behind.

Everything else is per-session state: the tray icon, the timers and the WinEvent
hooks all die with the process, so a killed Azy uses no CPU, no GPU and no memory,
and cannot block Premiere from starting again.
