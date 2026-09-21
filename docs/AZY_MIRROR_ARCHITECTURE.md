# Azy Skin — the mirror architecture

This is the document the code points at. It describes what the round-9 rebuild is,
how a pixel from Premiere Pro reaches the screen wearing Azy's skin, and — in §10 —
exactly where the model can be fooled. Nothing here is a promise about behaviour on
your machine: §11 lists what has actually been checked and what has not.

---

## 1. What Azy is now

Azy is a **live visual mirroring, rendering and composition system for the real
Premiere Pro window**.

Windows Graphics Capture hands Azy the live pixels of the Premiere window on the GPU.
A compute-free pixel shader rebuilds those pixels as dark glass: panel surfaces,
1px lit borders, localised glow, a faint sheen, static grain. The result is presented
in a borderless click-through window placed exactly over Premiere's visible frame, in
front of it and never on top of anything else.

Premiere itself is untouched. It keeps drawing its own UI, it keeps the keyboard, the
mouse and the render path. Azy never puts a window inside it, never sends it input,
never edits its files, never reads its project. Delete `AzySkin.exe` and Premiere is
exactly as it was.

What Azy *cannot* do is know what a control is. §3 is the honest version of that.

## 2. The pipeline

```
Premiere main window (HWND validated against the Premiere PID)
        │
        │  Windows.Graphics.Capture  →  ID3D11Texture2D (GPU, shared)
        ▼
WindowCapture            src/win32/capture/window_capture.cpp
        │
        │  one texture, no CPU copy, no PNG, no disk, no screenshot
        ▼
MirrorRenderer           src/win32/mirror/mirror_renderer.cpp
        │  · a swap chain in a layered, click-through, no-activate window
        │  · one full-screen pass: mirror.hlsl
        ▼
DirectComposition        (presented through a composited swap chain)
        │
        ▼
the window the user sees — positioned over Premiere's visible frame
```

One thread of the Azy process drives all of it. There is no worker pool, no render
thread per monitor, no polling loop over HWNDs: capture hands frames to the texture,
a `WM_TIMER` at the pacing rate (§7) presents the latest one, and window events
re-position the mirror (§6).

## 3. How a pixel is classified (and what that means)

Premiere draws its workspace itself. An outside process gets a picture, not a tree:
the docked panels of a modern Premiere are *pixels*, not child windows, and the
accessibility tree is partial and version-dependent. So Azy classifies the way an eye
does, from three sources, in this order of authority:

1. **The panel model** (`build_panel_map`, `src/core/panel_map.cpp`) — rectangles for
   the application header, Project, Effect Controls, the two monitors, Effects, the
   right column and the Timeline, derived from the client rectangle, the DPI and the
   workspace layout. These are the only "known" regions. They are a *model*: they say
   where the big surfaces are, not what is inside them.
2. **Luminance** — inside a panel, pixels below the transition luminance count as
   surface; above it, as text or controls. That single threshold is what separates
   "panel interior" from "the labels on it", and the boundary is soft
   (`smoothstep`) so nothing snaps.
3. **Local structure** — four neighbour taps recover the local contrast that the
   darkening removed (clarity/mid-tone) and detect Premiere's own 1px control lines,
   which are then re-lit in the theme's border colour.

Two regions bypass classification entirely:

* **Media regions** (Program Monitor and Source Monitor picture areas, computed by
  `monitor_pass_through` including the monitor's own toolbar strip) are passed
  through *exactly*. Inside them the shader returns the captured pixel untouched —
  no tint, no blur, no grading. The design preview measures this per frame: the
  largest per-channel change inside a monitor is 0.0000.
* **Content protection** — a pixel that is both bright *and* vividly coloured inside a
  panel is far more likely to be a thumbnail, a preview or a waveform overlay than a
  control, and Premiere offers no API that says where those are. Such pixels
  (`smoothstep(0.30, 0.55, saturation) × smoothstep(0.45, 0.75, luma)`, kept at
  `content_keep = 0.88`) escape most of the treatment, softly. This is an
  approximation, and §10 says so.

Nothing is recreated: every pixel on screen is Premiere's own pixel, re-lit.

## 4. The shader contract

`resources/shaders/mirror.hlsl` is one pass, Shader Model 4.0 so the same source
compiles as `ps_4_0` and `ps_5_0`:

| stage | source |
|---|---|
| geometry | full-screen triangle, no vertex buffer, no input layout |
| capture | `Texture2D` point-sampled at `g_uv_min..g_uv_max` (pixel-exact copy) |
| diffusion | the same texture at mip 4, linear-sampled (the glass "blur") |
| clarity | four neighbour taps, DIP-scaled by `g_dpi` |

The constant buffer (`cbuffer AzyMirror : register(b0)`) and the C++ struct
`MirrorParams` are the same layout, member by member: 464 bytes, 16-byte aligned
blocks, float2/float4 arrays only. `tools/check-mirror.py` parses both and fails the
build check when they disagree — a mismatch here is invisible in review and shows up
as wrong colours on screen, which is the worst possible failure mode.

Slot counts are part of that contract: `PASS_SLOTS`/`kMirrorPassSlots` = 4 media
regions, `PANEL_SLOTS`/`kMirrorPanelSlots` = 8 panel frames. Slot flags are arrays of
`float4` on both sides (`g_active[i >> 2][i & 3]`) because a vector array is the one
shape whose packing is unambiguous.

## 5. Window rules

The mirror window is created with:

```
WS_POPUP                                no frame, no caption, no system menu
WS_EX_LAYERED | WS_EX_TRANSPARENT       click-through across processes
WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW     never takes focus, never in Alt+Tab
WS_EX_NOREDIRECTIONBITMAP               it composites, it does not redirect
```

and *not* `WS_EX_TOPMOST`, *not* `HWND_TOPMOST`. Azy inserts itself directly in front
of Premiere's window (`SetWindowPos(..., hwnd_premiere, ...)` inserts *after* that
window, i.e. in front of it) so it sits above Premiere and below everything the user
raises above Premiere. `tools/check-mirror.py` fails if any of those styles or calls
appear in the source.

Placement follows the visible frame: `overlay_rect` intersects Premiere's frame with
the monitor's work area when maximized (the maximized-window overhang is invisible by
design) and with the monitor when fullscreen.

## 6. Staying in sync

`SkinEngine` is an explicit state machine, not a set of booleans:

```
WAITING_FOR_PREMIERE → PREMIERE_FOUND → WINDOW_VALIDATED
      → CAPTURE_INITIALIZING → CAPTURE_ACTIVE → MIRROR_ACTIVE
                  ↘ CAPTURE_FAILED (retry)  ↘ UNSUPPORTED (manual retry)
                  ↘ SUSPENDED (minimized / hidden / inactive)
```

Events come from (a) Azy's own WinEvent hook on the Premiere main window — move,
resize, minimize, restore, destroy, name/identity change — and (b) the capture
session's own callbacks for content size changes and closure. Movement and resizing
are re-positioned in a burst (0.6 s of faster updates after the last event) so a drag
does not repaint at event rate.

**No recursive capture.** Azy captures the Premiere window only, validates the HWND
against the Premiere process at every important operation, and `<pid>==GetCurrentProcessId()`
guards are checked in review. Because the mirror is never inside the captured tree
(it is a separate top-level window that Azy draws), no feedback loop exists — and the
guard means a mistake would be refused rather than looping.

When Premiere closes, the capture session is stopped and every GPU object is released.
When it restarts, the state machine returns to `WAITING_FOR_PREMIERE` and reconnects
without restarting Azy.

## 7. Cost, on purpose

The session is paced, not free-running:

| state | normal | performance mode |
|---|---|---|
| active (input or resize within 2 s) | 60 fps | 30 fps |
| idle | 15 fps | 8 fps |

There is no timer at all while the mirror is hidden: the paused state costs nothing.
Suspension reasons are explicit — minimized, hidden (virtual desktop/cloaked),
inactive, dragging, moving. Capture is WGC-driven: frames are produced when the source
produces them, so an idle Premiere produces no work for Azy beyond the timer that
presents the last frame — and the same frame is not re-presented when nothing changed.

The rendering itself is one pass, GPU-only, with no readback, no staging copy, no
allocation per frame: the same constant buffer and the same two textures are reused.

## 8. From theme to pixels

No colour literal exists in the renderer. The chain is:

```
SettingsStore (theme key, sliders)
   → theme_tokens()          the 13 §26 tokens, per theme key
   → mirror_style_from_settings()  scalars: darkness, surface, glass, radius, glow…
   → ScaledMirrorStyle       × fade, × DPI scale
   → MirrorParams            the constant buffer
   → mirror.hlsl             the material
```

The 13 tokens are Background, Surface, SurfaceSecondary, Border, BorderActive,
Accent, AccentSecondary, Glow, Selection, Hover, TextPrimary, TextSecondary,
TextDisabled. Ten theme keys exist (Blue/Purple, Cyan, Purple, Magenta, Red, Orange,
Green, Pink, Custom, Original); `Original` maps to `mirror_style_is_passthrough()` and
means "show Premiere untouched", which is how Azy can be visually disabled without
being stopped. Switching a theme changes the constant buffer on the next frame — no
rebuild, no restart. Themes are added as configuration: a table row in
`theme_tokens.cpp`, nothing in the renderer.

Performance mode changes *strengths* (no diffusion, no gloss, no grain, less glow),
never readability and never content protection.

## 9. Failure states, stated honestly

| state | what the user sees | what the log says |
|---|---|---|
| no Premiere running | nothing (no window) | `waiting for Premiere Pro` |
| Premiere running, no main window yet | nothing | `Premiere Pro is running` |
| capture refused by Windows | nothing | `this Windows build has no window capture API` |
| capture failed repeatedly | nothing after the current attempt | `the capture failed repeatedly; the log has the reason` |
| Premiere minimized/hidden | nothing | suspension reason |
| Premiere closed while mirroring | nothing | `the window no longer belongs to that process; reconnecting` |
| theme = Original | Premiere exactly as itself | passthrough |

"Nothing shown" is the design rule for every failure: Azy never leaves an opaque or
stale rectangle over Premiere. Debug mode (§38 of the brief) prints one grouped
diagnostics block with lifecycle state, Premiere detection, window geometry, DPI,
capture and renderer statistics, composition mode, theme values, measured rates, CPU
cost and the last error.

## 10. Where this model can be fooled

Written down because the brief demands honesty about approximation, and because every
item here is a place where the skin will look wrong in a way that is *not* a bug:

1. **A dark thumbnail is treated as panel surface.** The luminance threshold cannot
   tell "dark image in the Project panel" from "panel background".
2. **A bright, vivid pixel inside a panel escapes most of the treatment** — that is
   content protection working, but it means a saturated *control* (a coloured button,
   an active accent element inside a panel) gets less skin than its neighbours.
3. **A grey, low-saturation preview image is not protected** and will be darkened like
   a panel surface.
4. **Panel rectangles are a model.** If Adobe moves or renames a panel, or the user
   rearranges the workspace beyond the layouts the model knows, frames are drawn in
   the wrong place. The model is conservative: a rectangle it is unsure about is left
   out (`usable = false`) rather than drawn.
5. **Premiere's own 1px control lines and text edges both produce gradients.** Re-lit
   text is not mistaken for a border, but very small text does pick up a little of the
   border colour.
6. **Monitor toolbar geometry is a DIP offset**, not a measurement: on an unusual
   panel size or a future version the strip can be a few pixels off, which shows as a
   hairline location, never as skin over the picture (the picture region is inset, not
   expanded).
7. **Undocked panels are real windows.** A panel floating outside the main window is
   not covered by the mirror at all — it stays Premiere's own.
8. **The model was not validated against a running Premiere.** Everything above is
   about the *method's* limits; the numbers used in it (panel proportions, the toolbar
   height) are the ones a debug screenshot has to confirm or correct. That screenshot
   has not happened.

## 11. What is verified, and what is not

**Statically verified** (this machine, no Windows runtime):

* the shader's constant buffer and the C++ struct agree member by member
  (`tools/check-mirror.py`), including a mutation test of the structural checks;
* the window styles and capture guards above are present in the source and enforced by
  the same check;
* the core maths (panel model, pass-through geometry, packing, style derivation, theme
  derivation, DPI/radius rules) is covered by 747 native unit assertions that pass;
* the whole Win32 layer cross-compiles and links as a GUI executable, and the PE
  inspection confirms the manifest and resources;
* a **design preview** (`tools/preview_render.py`) re-implements the shader on the CPU
  and renders `docs/images/azy_mirror_before_after.png` from a synthetic Premiere
  picture: media pass-through delta 0.0000, mean luminance 0.31 → 0.26 (0.84×),
  text-edge contrast 0.0256 → 0.0267. It is a picture of the maths, not a screenshot.

**Not verified — requires a real Premiere Pro on Windows:** capture start, frame
arrival, latency, present smoothness, everything about focus and click-through,
move/resize/minimize/deflate sync, multi-monitor and DPI behaviour, idle CPU/RAM, and
whether the panel model matches the user's workspace. No claim in this repository
should be read as runtime-proven until that pass happens; the labels used are
IMPLEMENTED (statically verified), RUNTIME UNVERIFIED.

## 12. File map

| file | role |
|---|---|
| `src/win32/capture/window_capture.cpp` | WGC session, free-threaded frame pool, GPU texture |
| `src/win32/capture/wgc_abi.hpp` | the WinRT interfaces, declared by hand (no WinRT SDK in the cross toolchain) |
| `src/win32/mirror/mirror_renderer.cpp` | window, swap chain/DComp, pacing, present |
| `resources/shaders/mirror.hlsl` | the material (the skin) |
| `include/azy/win32/mirror/mirror_renderer.hpp` | `MirrorParams` (the constant buffer contract) |
| `src/core/mirror_style.cpp` | settings → scalars |
| `src/core/theme_tokens.cpp` | the 13 tokens, 10 theme keys, hex parsing |
| `src/core/panel_map.cpp` | the panel model |
| `src/core/capture_math.cpp` | UV mapping, pass-through regions, panel frames, packing |
| `src/win32/skin/skin_engine.cpp` | the state machine |
| `tools/check-mirror.py` | the contract check |
| `tools/preview_render.py` | the design preview (CPU re-implementation) |
| `tools/dump_style.cpp` | dumps tokens/style/geometry for the preview and for manual inspection |
