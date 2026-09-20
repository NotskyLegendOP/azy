# Azy Skin — the duplicate window overlay

**Status of every claim in this document: IMPLEMENTED — RUNTIME UNVERIFIED.**
Nothing here has been executed against a real Premiere Pro on a real Windows
machine. `verify.sh` proves that the code compiles, links and passes 649 portable
checks; it cannot prove that a single pixel reaches the screen. Where the wording
matters, the claim labels are: **VERIFIED** (proved by a command run in this
repository), **IMPLEMENTED — RUNTIME UNVERIFIED**, **UNVERIFIED**, **HIGH RISK**.

---

## 1. Overview

### 1.1 What changed and why

Rounds 1–7 built a skin that decorated Premiere from the outside: DWM window
attributes for the title bar, four thin click-through strips for a ring, and one
translucent sheet over the whole window. It never touched the pixels Premiere
draws. The result was reported as "not visible" three times, most recently with
"make the layers visible — how will the layout appear if it's transparent".

The duplicate overlay is a different idea: **do not restyle Premiere's UI — show
its pixels again, skinned, in a window of Azy's own.**

```
   +-----------------------------------------------------+
   |  Premiere's real window   (all input, all state)    |   <- functional app
   |  capture (GPU, window-scoped)                       |
   +-----------------------|-----------------------------+
                           v
   +-----------------------------------------------------+
   |  Azy's duplicate window   (no input, no focus)      |   <- presentation only
   |  capture texture -> shader -> DirectComposition     |
   +-----------------------------------------------------+
```

The relationship is one-way and is never reversed (§43 of the brief): Premiere
keeps the mouse, the keyboard, the focus, the caret, the menus, the timeline, the
playback, the plugins and the export. Azy only decides what the pixels look like.

### 1.2 The layers, and which one is in charge

| Layer | What it is | When it is used |
|---|---|---|
| **Duplicate window** (new) | A GPU-composed window above Premiere showing a skinned copy of its pixels | Whenever the capture is running, a frame has arrived and the style is visible |
| **Ring** (`composition_surface`) | Four 1px click-through strips on the window edge | Whenever the duplicate is *not* on screen |
| **Veil** (`overlay_veil`) | One translucent sheet over the window | Whenever the duplicate is *not* on screen |
| **Frame** (`dwm_composer`) | DWM caption/border colours on Premiere's own window | Always, while the window exists and the skin is on |

The duplicate is placed **directly above Premiere** and nothing else. The ring and
the veil are *deliberately switched off while it is up*: they would be behind it,
so drawing them would spend GDI work and GPU memory on pixels nobody can see. This
is the "avoid double rendering" rule, and it is why turning the feature on does not
roughly double the skin's cost.

### 1.3 The decision the brief asked for (§34)

> "Decide whether this architecture is achievable with a reasonable capture and
> composition API, or whether another native technique is more reliable."

**Verdict: achievable, and it is the only approach outside injection that gets the
result the brief asks for — but it is not the most reliable way to *decorate* a
window, so the previous architecture stays as the fallback.**

The APIs considered:

| Technique | Why not (or why) |
|---|---|
| **Windows Graphics Capture** (chosen) | Per-*window*, GPU-to-GPU (a D3D11 texture arrives; no CPU copy), event-driven arrival, no injection, no privileged helper. This is the only supported way to read one window's pixels at composition time. |
| **DXGI Desktop Duplication** | Captures a whole **monitor**: it would include the duplicate window itself (infinite mirror), the taskbar and unrelated applications, and it cannot be scoped to one window. The brief forbids capturing anything but Premiere. Rejected. |
| **DWM thumbnails** (`DwmRegisterThumbnail`) | The cheapest possible mirror — the compositor copies the source window into ours with no work on Azy's side. Rejected because a thumbnail is exactly what it says: **we cannot put a shader between that copy and the screen**, so we could not skin the mirrored pixels, could not leave the Program Monitor untouched, and could not darken anything. It would produce the transparent duplicate the user has already rejected. Worth remembering if a zero-cost mirror is ever wanted. |
| **GDI `PrintWindow` / `BitBlt`** | CPU bitmap copies and polling, and unreliable for GPU-composited windows. Forbidden by the brief and by the performance budget. Rejected. |
| **DirectComposition** (chosen for presentation) | GPU composition with premultiplied alpha, no `UpdateLayeredWindow` CPU copy, rounded-corner alpha and a pixel shader in the path. |
| **Injection / API hooking** (present hooks, detours) | The only way to restyle Premiere's *widgets* rather than their pixels. Permanently out of scope (class D). |

The honest summary, in one paragraph: **the capture approach can skin the pixels
but not the widgets.** Azy can darken, tint, gloss, separate and frame what
Premiere draws; it cannot change a button's shape inside Premiere, because that
would require being inside Premiere. If a future version wants widget-level
theming, the answer is "no" — not "more hooks". The capture path also costs more
than the static path (GPU memory, a worker thread, a small frame latency), so the
static path is kept for hosts where the capture cannot run.

---

## 2. Capture method

* **API:** `Windows.Graphics.Capture` (`IGraphicsCaptureItemInterop::CreateForWindow`),
  with `Direct3D11CaptureFramePool::CreateFreeThreaded`.
  IMPLEMENTED — RUNTIME UNVERIFIED.
* **What is captured:** exactly one window — the tracked Premiere top-level window,
  the one the window tracker already follows. Never the desktop, never a monitor,
  never another application. The capture API is asked for a *window* and Azy never
  calls the monitor form (`tools/check-overlay.py` fails the build if
  `CreateForMonitor` appears anywhere in `src/`).
* **Ownership check before use:** the handle comes from the tracker, and
  `WindowCapture::start()` re-validates it: `IsWindow`, its owning process id must
  equal the tracked Premiere pid, and it must not belong to Azy itself. A recycled
  handle therefore cannot make Azy mirror an unrelated application. IMPLEMENTED —
  RUNTIME UNVERIFIED.
* **Why the ABI is hand-declared:** the toolchain (zig's bundled MinGW) ships
  `d3d11.h`, `dxgi1_2.h`, `dcomp.h`, `roapi.h`, `hstring.h` and `inspectable.h`,
  but no `windows.graphics.capture.h` and no C++/WinRT. `include/azy/win32/capture/wgc_abi.hpp`
  declares the five interfaces used, with the IIDs and vtable order taken from
  published sources (the SDK header text quoted in
  microsoft/Windows.UI.Composition-Win32-Samples issue #97, the WinRT bindings, and
  Microsoft's own documentation). Every IID, every vtable slot and the fact that
  **cursor capture defaults to on** (which would draw a second cursor inside the
  mirror) are recorded as comments next to the declarations. This is the highest
  risk in the whole feature and is treated as such (see §11).
* **Frame format:** `B8G8R8A8_UNORM`, the format DWM composes in — the same format
  Azy's composition swap chain uses, so no conversion exists anywhere in the
  pipeline.
* **Where the pixels go:** the arrived frame's D3D11 texture is copied on the GPU
  into a texture Azy owns (`CopySubresourceRegion` of the frame's content box, or
  `ResolveSubresource` when the captured surface is multisampled). The frame is
  then released. **No CPU copy, no staging texture, no `Map`, no file, no PNG,
  no screenshot.** The only read-back of pixels anywhere in Azy is the
  user-triggered "Check visibility" sampler, which reads a few dozen pixel values
  from Azy's own GDI layers, not from this pipeline.
* **Pacing:** a worker thread polls `TryGetNextFrame` no more often than the
  current pacing (10 fps idle, 30 fps active; 5/24 in performance mode). Frames
  arriving faster than the pacing are dropped unread — they cost nothing, because
  the desktop underneath is already composing the real window.
* **The cursor** is switched off through `IGraphicsCaptureSession2`, because the
  compositor draws it rather than the window; without that, the mirror would show
  a second cursor whenever the pointer is over Premiere.

---

## 3. Overlay method

* **Window:** `WS_POPUP` with `WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP`.
  No taskbar entry, no alt-tab entry, no activation, transparent to hit testing,
  and no GDI redirection surface (the compositor owns every pixel).
* **Position:** `SetWindowPos` with the window in front of Premiere as
  `hWndInsertAfter` (`z_order_anchor`), which places the duplicate **directly above
  Premiere and nowhere else**. It is **not topmost**, so another application
  brought forward still covers it — the desktop does the hiding for free, and
  "above Premiere" never becomes "above everything".
* **Size:** the DWM extended frame bounds (the rectangle the user actually sees),
  intersected with the monitor work area when the window is maximized and with the
  monitor when it is fullscreen — a maximized window's frame is deliberately
  reported as larger than the display, and using it verbatim would put the
  duplicate off the edge. This is `overlay_rect()` in the portable core, unit
  tested.
* **Presentation:** a flip-model `CreateSwapChainForComposition` swap chain with
  `DXGI_ALPHA_MODE_PREMULTIPLIED`, attached to an `IDCompositionVisual` whose root
  is the window. The shader writes premultiplied alpha; DWM blends it.
* **Drawing:** one full-screen triangle, one draw call, no vertex buffer and no
  input layout to keep in sync; the vertex shader derives the triangle from
  `SV_VertexID`. Blend state is unnecessary because the buffer is fully rewritten
  each frame (the rounded corners are alpha, not blending).
* **What the shader does** (`resources/shaders/gloss.hlsl`, embedded into the
  executable at build time):

  | Step | Effect |
  |---|---|
  | Sample | The captured texture through the sub-rectangle the duplicate covers (fractions of the captured rectangle, not pixels) |
  | Pass-through | The Program and Source Monitor rectangles keep **exactly** what Premiere drew, feathered by ~1.5 px. The footage is never darkened. |
  | Charcoal | Mix towards the theme's own veil colour, then multiply by darkness, then lift the blacks a little (that lift is what makes it read as glass rather than as a dimmed screenshot) |
  | Gloss | A wide, low-contrast diagonal sweep |
  | Accent | A trace of the theme's accent, strongest near the header, never a neon edge |
  | Depth | A gentle vignette from the window edge inwards |
  | Separators | 1px hairlines on the panel model's boundaries, excluding the pass-through regions |
  | Frame | The 1px *lighter* bezel the whole visual language is built on, plus the accent on the top edge |
  | Corners | Rounded-rectangle SDF alpha, radius from `corner_radius_dip` (8 by default), clamped so it can never become the 20–30px pill the brief rules out |

  This is not a colour filter: the UI is darkened, tinted, glazed, separated and
  framed while the picture regions are untouched, and the geometry comes from the
  same panel model the rest of Azy uses. It is also not a pretend-perfect
  segmentation: the model is a ratio layout, and where it is wrong the result is a
  hairline in the wrong place — never a broken or empty window (§10).
* **Style source:** `make_overlay_style()` in the portable core derives every
  shader constant from the *existing* palette and sliders (darkness, overlay
  strength, glass, border, shadow, accent, corner radius). There is no second set
  of controls and no second colour language. **Performance mode** keeps the
  structure (bezel, hairlines, darkening) and drops the GPU extras (sheen, grain,
  vignette) — unit tested.
* **No shader compiler, no overlay:** the pass is compiled at start-up from source
  through `d3dcompiler_47.dll` (SM 5.0, then SM 4.0; loaded dynamically, so
  nothing new is linked). If that DLL is missing or the compile fails, the
  duplicate is not created and the ring/veil stay in charge.

---

## 4. Input

* **Mouse:** the duplicate never receives it. The window is created with
  `WS_EX_LAYERED | WS_EX_TRANSPARENT`, which is what makes Windows skip it during hit
  testing **across processes** — `WM_NCHITTEST` answering `HTTRANSPARENT` only
  forwards a hit to windows of the *same thread* (documented behaviour), so that
  alone could not carry a click into Premiere. The window procedure returns
  `HTTRANSPARENT` as well, for the same-process case, and `WM_MOUSEACTIVATE`
  returns `MA_NOACTIVATE`. Mouse down, drag, wheel, double-click, right-click and
  timeline scrubbing therefore reach the real Premiere window underneath, unchanged.
  IMPLEMENTED — RUNTIME UNVERIFIED.
* **The constant alpha is 255 on purpose.** A layered window has to state one
  (`SetLayeredWindowAttributes`). 255 changes nothing: the skin's transparency comes
  from the compositor's own premultiplied alpha, and a smaller value would dim the
  whole mirror. If a future report says the duplicate looks dimmed, this is the
  first line to look at.
* **The one combination that is not documented together.** `WS_EX_LAYERED` and
  `WS_EX_TRANSPARENT` are required for cross-process click-through (above);
  `WS_EX_NOREDIRECTIONBITMAP` is required so there is no GDI surface that could be
  painted black behind the mirror. Microsoft documents each of the three with
  DirectComposition, and its DirectComposition *layered child window* sample uses
  the layered bit too, but the three are not documented *together* on a top-level
  window. The failure mode was chosen deliberately: with no redirection bitmap
  there is nothing to paint, so an unsupported combination shows nothing — the ring
  and the sheet are put back — rather than an opaque box over Premiere. Test plan
  §3.1 is the check for it.
* **Keyboard:** nothing in the capture or composition path touches keyboard input.
  There is no keyboard hook, no `RegisterHotKey`, no message filter. `WM_SETFOCUS`
  is handled only to give focus back to the tracked window if Windows ever hands it
  over — `WS_EX_NOACTIVATE` means it should never happen.
* **Focus:** the duplicate can never become the foreground window
  (`WS_EX_NOACTIVATE` + `SWP_NOACTIVATE` on every placement).
* **Azy's own controls:** the duplicate has none. Any future control (a slider, a
  hover strip) would have to live in a *separate, explicitly interactive* layer —
  separate window, separate styles — so that the mirror itself stays
  mouse-transparent forever. Nothing like that is implemented today.

---

## 5. Synchronisation

* **Geometry follows the window.** The tracker produces a `SkinTarget` per event
  (move, resize, maximize, restore, display change, DPI change) and every `apply()`
  hands the duplicate the new rectangle. `GlossFrame::operator==` makes an
  unchanged frame free: no `SetWindowPos`, no `ResizeBuffers`, no present.
* **Resize debounce.** Geometry changes come from the existing tracker, which
  already ignores the 1px churn Windows produces while dragging; the swap chain is
  resized only when its size actually changed (`ResizeBuffers` is skipped when the
  descriptor already matches), and the capture's frame pool is recreated in place
  when the captured content size changes — the *renderer is never torn down for a
  resize*.
* **Minimize / restore.** Minimized or hidden: the duplicate is hidden and the
  capture is stopped (raising `IsIconic` to `CaptureStart::Minimized`, which is
  treated as an expected state, not an error). Restore: the tracker reports the
  window again and the capture restarts.
* **Close / restart.** When the target disappears the engine calls
  `teardown_overlay()`: the capture worker is joined, the D3D11 device, the swap
  chain, the composition target, the shader and the textures are released. When
  Premiere starts again the detector reports a new window, and the overlay is
  created and attached again **without restarting Azy**.
* **First appearance.** The duplicate is only shown once the capture has actually
  produced a frame (`frames > 0`). An empty composition window would be a hole
  where Premiere's UI should be — the exact complaint that started this round.
  While it waits, the ring and the veil are still on screen, so the skin never
  blinks off.
* **Frame latency.** One capture frame plus one present: ~16 ms at 60 Hz capture
  and 30 Hz present, up to ~50 ms in the idle pacing, before any compositor delay.
  That is measured nowhere yet (see §11); it is an estimate from the pacing
  settings, not a measurement.

---

## 6. Performance

Design rules, all of them implemented:

* **No CPU pixel work.** The capture hands over a GPU texture; the frame is copied
  GPU-to-GPU; the shader writes a composition buffer; DWM composites. Nothing
  allocates or copies pixel data on the CPU.
* **No polling in the steady state.** The duplicate's timer is *armed by work, not
  by time*: it is re-armed only when the pacing changes, and it is removed entirely
  (`KillTimer`) while the window is hidden — "Suspend skin" costs zero here. When
  the timer does run, a tick with nothing to do is one flag read (the "is there a
  fresh frame" check) and no GPU work.
* **Smart modes.** Pacing is driven by the capture itself: frames keep arriving →
  the window is changing (playback, scrubbing, a menu) → 30 fps (24 in performance
  mode); frames stop → 10 fps (5), where each tick is a flag read. A single fresh
  frame keeps the fast pacing for 1.5 s, and a geometry change (a move or resize)
  requests a burst so the mirror does not lag behind the drag.
* **No permanent high-rate loop.** The highest rate anywhere is 30 presents and 30
  captures per second, and only while frames are arriving.
* **Reuse, not recreation.** One D3D11 device, one swap chain, one staging
  texture, one constant buffer, one sampler, two shaders. A resize reuses the
  device and the shaders and only resizes buffers; the staging texture is
  recreated only when the captured size changes.
* **Zero residual cost when stopped.** `revert()`/`shutdown()` hide the window,
  kill the timer, join the capture worker and release every GPU object; the device
  itself is released with the overlay. Nothing is left running after "Exit" or
  after Premiere closes.
* **The window-sized allocations do not survive being hidden.** Hiding the duplicate
  (skin off, suspended, minimized, Premiere gone) releases the staging texture and
  shrinks the swap chain to 1x1, so an idle Azy holds the device and the compiled
  shaders - the small things that make turning the skin back on instant - and not two
  full-screen surfaces. Everything is resized back on the next show.
* **Budget:** the executable grew from 559,616 to 614,912 bytes (61.4% of the 1 MB
  budget) — VERIFIED by `tools/inspect-pe.py`. No CPU or GPU percentage is claimed
  anywhere: none has been measured on a real machine, and the brief forbids numbers
  that were not measured.

---

## 6b. The debug screen's field list

The brief lists the fields the overlay's debug view has to show. Where they are
available they are **measured**, and where they are not the line says so instead of
estimating. Diagnostics are visible in Settings → *Diagnostics* (and logged when
debug mode is on); they are read on demand, so nothing here costs anything while the
user is not looking.

| Field | Where it comes from | Status |
|---|---|---|
| Premiere's window handle and process id | the tracker's target + `GetWindowThreadProcessId` | measured |
| The duplicate's window handle and Azy's process id | `GetCurrentProcessId` + the overlay's `HWND` | measured |
| Premiere's X/Y/W/H | `SkinTarget::visible_frame` | measured |
| The duplicate's X/Y/W/H, and the offset between the two | the last placement the overlay made | measured — the offset should be `0,0` |
| DPI | the tracker's per-monitor DPI | measured |
| Monitor rectangle | the tracker's monitor bounds | measured |
| Capture state | the capture's own flags (`running`, `item closed`, unsupported) | measured |
| Captured frames, GPU copies, empty polls, failures, frame-pool rebuilds | counters in the capture worker | measured |
| Capture rate and draw rate | two diagnostics reads, divided by the wall time between them | measured — the first read says "no previous sample yet" |
| Newest captured frame's age | the worker stamps every frame it copies | measured |
| Render pacing (idle/active) and the pass-through/panel counts | the overlay's own stats | measured |
| Overlay state (`on screen`, note, supported on this host) | the engine's overlay report | measured |
| GPU resources | the *sizes* of the swap chain buffers and the capture texture | reported as an inventory only — no VRAM figure is claimed, because the API exposes no counter for it. The line names the buffers as "while shown", since a hidden duplicate holds neither them nor the staging texture |
| Process CPU | `GetProcessTimes` deltas between two diagnostics reads | measured, as a percentage of one core |
| Capture latency | — | **not reported.** Windows Graphics Capture gives no trustworthy per-frame timestamp in this code path, so the nearest honest figure is the newest frame's age |

---

## 7. Error recovery

| Failure | What happens | State afterwards |
|---|---|---|
| The capture ABI is missing (older Windows, locked-down build) | `window_overlay` is marked unsupported for the session, one warning line, no retry storm, no failure counter, no Safe Mode | Ring + veil carry the skin |
| The D3D11 device cannot be created (no hardware device) | Overlay disabled after three attempts; the reason is in the log and in the diagnostics | Ring + veil |
| `CreateForWindow` refuses the window | Retried with backoff (4 s, 8 s, 12 s…), the reason logged; after three attempts the overlay is disabled for the session | Ring + veil |
| The capture stops delivering frames (240 consecutive failures) | The worker ends, reports the reason; the engine sees a stopped capture and re-attaches on the next apply | Ring + veil, then retry |
| The frame pool outlives a window resize | `Recreate` is called with the new content size on the worker thread | Mirror continues at the new size |
| The GPU device is lost (`DXGI_ERROR_DEVICE_REMOVED`/`RESET` on present) | The overlay flags itself for recreation; the engine stops the capture, destroys the overlay (and with it the lost device, the swap chain, the shaders and the textures) and rebuilds everything on the next apply, with the backoff cleared so it happens immediately | Mirroring again a moment later — the ring and the veil cover the gap |
| Premiere is minimized | The duplicate hides, the capture stops, `Minimized` is not an error | Restores automatically |
| Premiere is closed | Capture worker joined, all GPU resources released | Nothing held |
| Premiere is restarted | New window, new capture, no Azy restart needed | Mirroring again |
| DPI change / monitor change | New geometry from the tracker, swap chain resized, UV recomputed | Mirroring again |
| The style would change nothing (Original theme, or every slider at zero) | The overlay is not created at all (`overlay_style_is_passthrough`) | Ring + veil (which are also off for Original) |
| Premiere runs elevated, Azy does not | Unchanged from earlier rounds: Azy's windows cannot be placed above an elevated window (UIPI). The tray offers "Restart as Administrator" | Reported as `partial` |

**The rule behind the table:** a failure of the duplicate may never be the reason
the skin disappears. Every path above ends with either the mirror working or the
previous layers doing their job.

**What is *not* handled this way: a hard process crash.** If Azy is killed or
crashes outright there is no marker file and no watchdog on the next start; it simply
starts again and tries the mirror again. That is a deliberate non-goal for 1.3.0 —
the overlay has no persistent state that could be left inconsistent, the capture dies
with the process, and every Windows-side change Azy makes to Premiere's window is
read-and-restored rather than owned. The open question a crash would raise is a
*cause*, not a cleanup: if a real run ever crashes while mirroring, the log up to
that point is the artefact to send.

**Deliberate design decision worth stating:** overlay problems do **not** feed the
failure tracker that can trip Safe Mode. A missing capture API is a property of the
machine, and pushing a user whose Windows build lacks the free-threaded frame pool
into Safe Mode (which disables composition surfaces they *can* use) would be worse
than the problem. The state is visible in the diagnostics, and "Check visibility" in
the settings window retries it.

---

## 8. DPI

* All geometry is physical pixels; the duplicate is placed with physical pixel
  coordinates from DWM's extended frame bounds, so it lines up at 100 %, 125 %,
  150 %, 175 % and 200 %.
* The shader receives `g_dpi` (device pixel ratio) and multiplies the 1px
  hairlines, the bezel width and the feather by it, so a hairline stays one
  *physical* pixel and a 1px edge is never resampled into two grey ones. The corner
  radius is converted from DIP to physical pixels by the same helper the rest of
  Azy uses (`dip_to_px`).
* Sampling uses a linear sampler with clamp addressing: at 1:1 the extra samples
  land on the same texel (nothing is blurred), and clamping prevents a 1px edge
  from wrapping into the opposite side of the window.
* Per-monitor DPI v2 awareness comes from the manifest; the tracker reports the
  DPI of the monitor the window is on, which is what the overlay is given.
* **UNVERIFIED:** that the capture texture's size in pixels matches the window's
  rectangle in pixels on a scaled display. Nothing in the API documents a
  guarantee, so when they disagree by more than 2 px the duplicate still draws —
  using the analytic mapping — and the mismatch is reported in the debug overlay
  and the diagnostics instead of being hidden.

---

## 9. Multi-monitor

* The tracker already follows the window across monitors and reports the monitor
  rectangle, the work area and the DPI of the monitor the window is on; the
  duplicate is placed with those numbers, so moving Premiere between two displays
  with different DPI and scale factors is handled by the same path as moving it on
  one.
* The duplicate never spans monitors: it is exactly the window's visible rectangle.
* A maximized window on a secondary monitor uses that monitor's work area (the
  intersection is computed from the reported monitor, not from the primary one).
* The capture is bound to a window, not to a display, so a monitor being
  unplugged/rearranged does not invalidate it; it only changes the geometry.
* **UNVERIFIED:** behaviour when a window is dragged across monitors *during* a
  drag, and the exact moment Windows re-evaluates DPI for the window (the tracker's
  event stream is what drives it).

---

## 10. Known limitations

1. **Widgets are not restyled.** The skin is applied to a copy of the pixels.
   Premiere's buttons, sliders, tabs and text keep their shape, spacing and font.
   Changing those requires being inside Premiere (injection), which is out of scope
   permanently.
2. **A hairline can be in the wrong place.** Panel rectangles come from a ratio
   layout model, not from reading Premiere's layout. A specific build with an
   unusual workspace can move a hairline. The model marks panels it cannot fit as
   unusable instead of guessing.
3. **The mirror is a frame or two behind.** While dragging a panel divider or
   scrubbing, the duplicate shows what Premiere looked like a moment ago. Static
   UI is unaffected; fast motion is where this shows.
4. **The duplicate appears only when it has content** (~a few frames after the
   capture starts). Until then the ring and veil are shown, so there is no hole and
   no flash of an empty window.
5. **The capture border.** Windows draws a thin border around a captured window
   unless the OS allows it to be switched off (`IGraphicsCaptureSession3`).
   Where it cannot be switched off, Azy's window sits on top of that border and
   hides it — but the *mirror itself* may contain it on hosts where a border is
   drawn inside the captured pixels. Reported through `border_state` in the debug
   overlay.
6. **The window-style combination is the least-documented part of the feature**
   (`WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOREDIRECTIONBITMAP` on one top-level
   window, §4). It has a safe failure mode — nothing is shown, the static layers
   carry the skin — and a one-line knob in `gloss_overlay.cpp`.
7. **`d3dcompiler_47.dll` is required** for the composition shader; every Windows
   10 1809+ machine ships it, but where it is missing the duplicate is not created
   and the static layers are used.
8. **An elevated Premiere blocks placement**, exactly as in earlier rounds
   (UIPI). Unchanged, reported, and fixable by running Azy elevated.
9. **Animations are still not implemented.** The `Animations` setting remains a
   reserved key with no code path behind it; the brief asks for a static look, and
   the settings window now says so rather than offering a control that does
   nothing.
10. **Two cursors if cursor capture cannot be disabled** (older capture interface):
   reported in the diagnostics, not silently ignored.

---

## 11. Areas that are runtime-unverified

Everything below is **UNVERIFIED — REAL PREMIERE RUNTIME REQUIRED**. These are the
questions a single run on a real machine answers, and the test plan
(`docs/AZY_OVERLAY_TEST_PLAN.md`) is written so that one pass covers them.

1. Does `CreateForWindow` succeed on a Premiere window on the target machine, and
   does the first frame arrive?
2. Do the capture texture's pixel dimensions match the window rectangle at 100 %,
   125 %, 150 % and 200 % DPI? (A mismatch is the most likely source of a
   slightly-stretched mirror.)
3. Is the analytic UV mapping correct — i.e. does the mirror line up with the real
   window at the edges, including the maximized case where the frame overhangs the
   display?
4. Does the hand-declared ABI work in practice? (Wrong vtable order would not fail
   cleanly; the crash marker exists for exactly this case.)
5. Does the mouse really pass through — down, drag, wheel, double-click, context
   menu, scrub — with Premiere behaving exactly as if the duplicate were not there?
6. Is the duplicate really above Premiere and below other applications, and is it
   covered when another app is brought forward?
7. What does the mirror look like during playback? (Latency and tearing are
   possible; the pacing is designed to keep the drop-out rate low.)
8. Does the Program Monitor really come through undarkened, and does the feather at
   its boundary look like a boundary and not like a frame?
9. Does the panel model's hairline placement look right on a real Editing
   workspace, at the user's own window size?
10. Idle CPU/GPU with the duplicate up, and after "Suspend skin" / "Exit" — the
    zero-residual-cost claim.
11. The Premiere restart path (close and reopen while Azy keeps running).
12. Whether the Windows capture border is visible on the target build.
