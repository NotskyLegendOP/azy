# Azy Skin — Techniques, and why they are safe

> **Superseded in 2.0.0.** This document is the record of the round-8 (v1.3.0)
> implementation: DWM frame styling, the four-strip ring, the translucent sheet and
> the duplicate window built on top of them. All of that was deleted in the round-9
> rebuild — the skin is now a live GPU mirror of the Premiere window, described in
> [`AZY_MIRROR_ARCHITECTURE.md`](AZY_MIRROR_ARCHITECTURE.md). Kept because knowing
> what was tried, and why it was replaced, is part of the project's record.

The project rule is that any technique which touches another process or its
rendering must pass six tests before it is used:

1. **Supported by Windows** — documented API, documented behaviour, no private
   interfaces.
2. **Stable** — failures are detectable and non-fatal; the failure mode is "the
   effect is missing", never "the other application misbehaves".
3. **Reversible** — the previous state can be read beforehand and restored
   afterwards.
4. **Compatible with Premiere** — it cannot change how Premiere processes input,
   frames or files.
5. **Safe for user input** — it cannot consume a click, key, drag or scroll.
6. **Necessary for the visual goal** — if a cheaper or safer technique reaches
   the same look, the cheaper one is used.

Everything Azy does is below. Everything that was considered and rejected is at
the end, with the reason.

---

## Level 1 — Native window composition (v1.3.0 — no longer used)

### 1.1 `DwmSetWindowAttribute(DWMWA_USE_IMMERSIVE_DARK_MODE)` — dark title bar

| Test | Result |
|---|---|
| Supported | `dwmapi.h`, Windows 10 1809+ (attribute 20; attribute 19 on 1809–20H1) |
| Stable | Returns `S_FALSE`/`E_INVALIDARG` when unsupported — nothing else happens |
| Reversible | Read first with `DwmGetWindowAttribute`; restored to the previous value or to `FALSE` |
| Premiere-compatible | Pure non-client painting; Premiere's client area and message handling are untouched |
| Input-safe | Yes — appearance only |
| Necessary | Cheapest possible lever: makes the frame dark on every supported Windows version |

Azy tries attribute 20 and falls back to 19, which covers Windows 10 1809 (the
oldest supported host) through Windows 11.

### 1.2 `DWMWA_CAPTION_COLOR` / `DWMWA_BORDER_COLOR` / `DWMWA_TEXT_COLOR` — charcoaled frame

| Test | Result |
|---|---|
| Supported | Windows 11 22000+; `COLORREF` values, `DWMWA_COLOR_DEFAULT` (0xFFFFFFFF) restores the system choice |
| Stable | Rejected attributes are simply ignored; Azy logs once and continues with the default frame |
| Reversible | Each colour is read first; if the read fails, Azy restores `DWMWA_COLOR_DEFAULT` |
| Premiere-compatible | Affects the title bar and window border only |
| Input-safe | Yes |
| Necessary | This is what makes the frame match the rest of the theme instead of Windows' own grey |

### 1.3 `DWMWA_WINDOW_CORNER_PREFERENCE` — rounded frame corners

| Test | Result |
|---|---|
| Supported | Windows 11 22000+ (`DWMWCP_ROUNDSMALL` ≈ 8 DIP, which is the value that matches Windows 11's own radii) |
| Stable | Rejected when unsupported; ignored on maximised windows by Windows itself |
| Reversible | Read first; restored (default `DWMWCP_DEFAULT`) |
| Premiere-compatible | Non-client only |
| Input-safe | Yes — the hit-test region follows a rounded frame correctly |
| Necessary | Premiere itself draws square corners; this is the only supported way to round the frame, and it matches the `Original`-theme escape hatch |

Azy **never** rounds a maximised or fullscreen window (Windows' own behaviour, and
rounding a screen-filling window would clip its corners for no gain), and never
applies a radius larger than 20% of the shorter window side. Azy's own surface
mirrors DWM's 8 DIP radius whenever DWM rounded the frame, so the two lines agree
instead of showing two different corners.

### 1.4 `DWMWA_SYSTEMBACKDROP_TYPE` (Mica) — experimental, off by default

| Test | Result |
|---|---|
| Supported | Windows 11 22621+ |
| Stable | Rejected on older builds; skipped in performance mode and in Safe Mode |
| Reversible | Previous value is read and restored (default `DWMSBT_AUTO`) |
| Premiere-compatible | Composes behind the *window frame*, not behind the client area |
| Input-safe | Yes |
| Necessary | **Not necessary** — which is exactly why it is opt-in via Advanced → "experimental visual features" and off for everyone else |

Because Premiere's client area is fully opaque, a backdrop can only ever show
through the non-client frame. It is treated as a polish item for users who ask for
it, never as a default.

### 1.5 `DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)` — truthful geometry

| Test | Result |
|---|---|
| Supported | Windows Vista+ |
| Stable | Falls back to `GetWindowRect` on failure |
| Reversible | Read-only |
| Premiere-compatible | Read-only |
| Input-safe | Yes |
| Necessary | **Yes.** Since Windows 10, `GetWindowRect` includes an invisible resize border (≈7px). Without this call, a 1px border drawn at "the window edge" would land up to 8px outside the visible frame — exactly the misalignment the design forbids |

Azy uses the extended frame bounds for everything the user can see, and
`GetWindowRect` only as a fallback and for monitor association.

---

## Level 2 — Targeted lightweight visual surfaces (v1.3.0 — no longer used)

### 2.1 One click-through layered window (`UpdateLayeredWindow`)

Before 2.0.0, Azy's visual element was a single ring around the window edge, built
from four 1px-stroke layers:

| Layer | Purpose |
|---|---|
| 1px hairline on the frame edge | separation: the line that makes panel boundaries read |
| 1px raised bezel just inside it | legibility: a purely *dark* edge is invisible over Premiere's own near-black panels, so the edge is slightly *lighter* than the panel, at 5–17% white |
| soft dark falloff inward across the band | depth: a quadratic shadow, starting one pixel in so it never muddies the bezel |
| subtle translucent wash behind the first few pixels | the "glass" hint: a fade to nothing within 2–6px, never a solid strip |
| 1px top inner highlight | light falling on glass, not a glow |

![v1.3.0: the ring, drawn from the renderer's own maths](images/ring-preview.png)

The ring is split into four thin strips (top, bottom, left, right) and each strip
is painted into its own premultiplied 32-bit ARGB bitmap with GDI+ and presented
with `UpdateLayeredWindow`. Every strip is drawn in *frame* coordinates, so a 1px
line is exactly one pixel, the corners join seamlessly, and nothing is scaled or
stretched. The corner arcs belong to the horizontal strips, so no pixel is drawn
twice.

| Test | Result |
|---|---|
| Supported | Since Windows 2000/XP; the documented mechanism for per-pixel-alpha windows |
| Stable | Failures are local: if the surface cannot be created or painted, the DWM frame treatment continues alone |
| Reversible | 100% Azy's own window — destroying it removes every trace |
| Premiere-compatible | A separate top-level window; Premiere's own window and rendering are untouched |
| Input-safe | Guaranteed structurally (see below) |
| Necessary | Premiere's internal panels are drawn by Premiere itself; this is the only supported way to place a pixel-accurate hairline exactly over its frame edge without touching Premiere |

**Input-safety contract** (asserted at runtime by `input_guard::verify()`):

* `WS_EX_TRANSPARENT` — skipped during hit testing, so clicks reach Premiere.
* `WS_EX_NOACTIVATE` — can never be activated, can never take focus.
* `WS_EX_TOOLWINDOW` — no taskbar entry, no Alt+Tab entry.
* `WS_POPUP` with no owner; `GWLP_HWNDPARENT` explicitly cleared; `WM_MOUSEACTIVATE`
  returns `MA_NOACTIVATE`; `WM_NCHITTEST` would return `HTTRANSPARENT` if it were
  ever called (it is not, and Azy counts the calls to prove it).
* Azy never calls `SetForegroundWindow` on a surface, never installs a keyboard
  hook, and has no focusable window at all while idle — its message window is a
  never-shown `WS_EX_NOACTIVATE` popup.

**Geometry rules**, all enforced in code:

* Never larger than Premiere's visible frame — it can therefore never cover
  another application.
* Inserted directly **above Premiere** in the z-order, so it follows Premiere's
  own stacking (minimise, cover, virtual desktop switch) instead of floating.
* Band thickness = 10 DIP (3 DIP in performance mode), capped at a twelfth of the
  shorter window side with a 3px floor, so a small floating panel gets a
  proportionally small ring (a 300px panel keeps its 15px band; a 90px one gets 7px)
  instead of four thick strips that meet in the middle.
* Strip thickness = band + 2px, or the corner radius when that is larger, so a
  large radius is never clipped; the radius itself is capped at thickness − 1 for
  the same reason. Both come from one shared helper (`azy/core/ring_layout.hpp`)
  that the unit tests exercise.
* On a maximized/fullscreen window the ring runs along the physical screen edges,
  so the bezel is halved to avoid reading as a border drawn around the display.
* Hidden (not merely transparent) whenever the skin is off, suspended,
  Premiere is minimised/cloaked, or the user is dragging.
* `CS_HREDRAW`/`CS_VREDRAW` are **not** set, and the bitmap is only regenerated
  when the visual key changes: no per-frame painting, no resize flicker.

### 2.2 GDI+ for vector drawing, exactly once per change

GDI+ (`gdiplus.dll`, shipped with Windows) is used for anti-aliased rounded
rectangles, a 1px pen and a fixed-bucket shadow falloff. It is not a rendering
engine Azy runs continuously: the strips are painted once per geometry/appearance
change, cached, and handed to the compositor. No GPU work, no animation, no
per-frame path re-tessellation.

The falloff is generated as a fixed number of 1px strokes (the count is the band
thickness, not the window size), so a 4K window and a small dialog cost the same,
and in performance mode the whole ring collapses to two constant strokes.

### 2.3 Dwmapi per-window, never per-process

Azy only ever modifies the **single top-level window** it identified as the main
Premiere editor window. Floating panels and dialogs inside Premiere are children
of that window and are drawn by Premiere itself — Azy does not touch them, does
not enumerate them for modification, and does not attempt to place surfaces over
them (see [`LIMITATIONS.md`](LIMITATIONS.md) for why).

---

### 2.4 The whole-window overlay (`SetLayeredWindowAttributes`, one solid layer)

The ring decorates the edge; the overlay covers everything inside it, which is what
makes the application read as skinned rather than outlined. It is one more
click-through, non-activating, layered top-level window — created 1×1, moved over
the tracked window's visible frame, painted with a single solid charcoal fill, and
blended by Windows with a *constant* alpha (`LWA_ALPHA`).

The point of the constant alpha is what it is not: there is no bitmap anywhere, so a
4K window costs the same as a 600×400 one. A per-pixel ARGB layer of the same size
would be ~8 MB at 1080p and ~33 MB at 4K of Azy's own memory, and DWM would blend
every pixel of it on every change. A constant-alpha solid layer is one fill, once,
then nothing.

Safe because it is the same contract as every other Azy surface:

* `WS_EX_TRANSPARENT` + `WS_EX_NOACTIVATE` + `WS_EX_LAYERED` + `WS_EX_TOOLWINDOW`,
  verified on the live window (`InputGuard`), with `HTTRANSPARENT` from
  `WM_NCHITTEST` as a second line of defence — a click, drag or shortcut always
  reaches Premiere;
* bounded by Premiere's own visible frame: never the desktop, never another monitor,
  never another application;
* stacked directly above Premiere and directly *below* the ring, so the 1px hairline
  and the bezel stay crisp on top of the tint;
* static — no animation, no gradient, no invalidate loop: one repaint per colour or
  geometry change;
* hidden the instant the skin is suspended, and destroyed with the process.

Its honest limitation is in [`LIMITATIONS.md`](LIMITATIONS.md): covering the whole
window also covers the video monitors, which is why the strength is a slider
(default ~30% tint) and why the whole layer can be switched off.

---

## Level 0 — Detection and observation (used, and free)

| Mechanism | Purpose | Notes |
|---|---|---|
| `CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS)` | find Premiere by executable *name* | one snapshot (~1 ms) only when something possibly changed; no hardcoded install path |
| `QueryFullProcessImageName` | exact executable path, read-only | `PROCESS_QUERY_LIMITED_INFORMATION`, never a write-capable handle |
| `GetFileVersionInfo` / `VerQueryValue` | Premiere version from the executable's version resource | read-only; the numeric `VS_FIXEDFILEINFO` is preferred, the string form is the fallback |
| `SetWinEventHook` (out-of-context) | window create/destroy/show/hide/state/location, foreground, move-size start/end, minimise start/end, cloak, desktop switch | no DLL is injected into any process; the handler only sets atomic flags and posts one message |
| WMI `__InstanceCreationEvent` / `__InstanceDeletionEvent` on `Win32_Process` | instant Premiere launch/exit notification | optional: if WMI is disabled or unavailable, Azy falls back to snapshots driven by the WinEvent observer |
| `MonitorFromWindow` / `GetMonitorInfo` | monitor and work area | per-monitor, never assumes the primary monitor |
| `GetDpiForWindow` / `GetDpiForMonitor` | effective DPI of the monitor Premiere is on | resolved dynamically; older hosts fall back to the system DPI |
| `RegOpenKeyEx(HKCU\...\Themes\Personalize)` | is Windows in dark mode | read-only; used for host capability reporting |
| `FindFirstChangeNotification` + `RegisterWaitForSingleObject` | `settings.ini` edited by hand | no polling; the callback only posts a message |
| `RegisterWindowMessage(L"TaskbarCreated")` | Explorer restarted | Azy re-adds its tray icon; nothing else changes |
| `WM_SETTINGCHANGE`, `WM_DISPLAYCHANGE`, `WM_POWERBROADCAST` | theme, DPI policy, monitor layout, resume from sleep | broadcast to Azy's message window; triggers a re-resolve |

---

---

## Level 3 — The duplicate window (v1.3.0 — replaced by the mirror in §4)

The skin the user actually sees since v1.3.0 is a *copy* of Premiere's window,
drawn back skinned, in a window of Azy's own. It is the only technique in the
project that reads another application's pixels, so it is the one that gets the
closest reading of the six tests.

### 3.1 `Windows.Graphics.Capture` — GPU capture of one window

| Test | How it is satisfied |
|---|---|
| **Supported** | A documented Windows API (`Windows.Graphics.Capture`, Windows 10 1809+), used in its documented non-UWP form: the activation factory is obtained with `RoGetActivationFactory` and the item is created with the documented `IGraphicsCaptureItemInterop::CreateForWindow`. No injection, no hook, no private interface. |
| **Stable** | Every failure mode is "the effect is missing": if the capture cannot be created the duplicate is not shown, one warning is logged, and the ring and the sheet carry the skin. Windows can end a capture at any time (the item's `Closed` event, `ReachedConnectionLimit`-style failures, a device reset); each of those is handled by stopping the capture and re-attaching, never by escalating. |
| **Reversible** | The session, the frame pool, the item and the device are released when the capture stops; the target window is never modified - not one attribute, not one message, not one pixel of Premiere's is written by this technique. |
| **Compatible with Premiere** | The capture is read-only and window-scoped. It cannot change how Premiere processes input, frames or files; Premiere does not even learn that it is being captured (the capture indicator border, where the OS draws one, is the OS's own decoration). |
| **Safe for input** | The duplicate window is `WS_EX_LAYERED` + `WS_EX_TRANSPARENT` (the pair that makes Windows skip a window during hit testing *across processes*) + `WS_EX_NOACTIVATE` + `WS_EX_TOOLWINDOW`, and it answers `HTTRANSPARENT` as well; there is no keyboard hook anywhere in the project. The capture itself never sees input. |
| **Necessary** | It is the only supported way to get *skinned* pixels of another window without being inside that process. A DWM thumbnail would be cheaper but cannot be skinned at all (§"rejected" below); GDI screen capture is a CPU copy and forbidden by the performance rules; injection and hooking are permanently out. |

Additional hard rules enforced in code and in `tools/check-overlay.py`:

* **Window capture only.** The monitor form of the API (`CreateForMonitor`) appears
  nowhere in `src/`; the checker fails the build if it ever does. A monitor capture
  would include Azy's own window - an infinite mirror - and every other application.
* **Never Azy itself.** The capture validates the target's owning process id and
  refuses its own. Together with the rule above, nesting is impossible by
  construction.
* **Never a CPU copy.** The arrived frame is a D3D11 texture; it is copied on the
  GPU into a texture Azy owns and released. No `Map`, no staging texture, no
  screenshot, no file. A frame pool resize (after the window is resized) happens
  *after* the frame in hand has been copied and released, so a rebuild can never
  invalidate a texture that is being read.
* **The cursor is switched off** (`IGraphicsCaptureSession2`): the compositor draws
  the pointer, so without this the mirror would show a second one.
* **Pacing is bounded** at 10 frames per second while the window is static and 30
  while it is changing - driven by the capture itself, not by a free-running timer.

### 3.2 `D3D11` + `DirectComposition` — where the copy is drawn

One hardware D3D11 device with `D3D11_CREATE_DEVICE_BGRA_SUPPORT` (no WARP
fallback: a software rasterizer would burn CPU to show a skin), a flip-model
composition swap chain with premultiplied alpha, and one `IDCompositionVisual`
whose root is the duplicate window. The window has
`WS_EX_NOREDIRECTIONBITMAP`, so there is no GDI redirection surface behind it - the
compositor owns every pixel. Nothing is drawn with GDI in this path, and the
composition is bounded by Premiere's own frame, like every other surface Azy owns.

### 3.3 Runtime shader compilation (`d3dcompiler_47.dll`)

The composition pass is a real `.hlsl` file in the repository, embedded into the
executable at build time and compiled once at start-up (Shader Model 5.0, falling
back to 4.0). The DLL is loaded dynamically - nothing new is linked - and it ships
with every Windows 10 1809+ machine. If it is missing or the compile fails, the
duplicate is not created and the static layers are used: the failure mode is a
simpler skin, never a broken window.

## Level 4 — The mirror (2.0.0, used)

This is the only thing Azy draws now, and the three techniques below are one
pipeline: Windows Graphics Capture produces a GPU texture of Premiere's window,
D3D11 draws it through a shader, and DirectComposition presents the result in a
click-through window that tracks Premiere's visible frame. The full description is
[`AZY_MIRROR_ARCHITECTURE.md`](AZY_MIRROR_ARCHITECTURE.md); what belongs here is why
each piece is safe.

### 4.1 `Windows.Graphics.Capture` — one window, on the GPU

| Test | Result |
|---|---|
| Supported | Windows 10 1809+ (the free-threaded frame pool needs 1809); documented WinRT API, no undocumented interfaces |
| Stable | Every step returns an `HRESULT`; a refusal or a lost device is reported, backed off and retried a bounded number of times, and the mirror is simply not shown |
| Reversible | The capture item is bound to one HWND; the session and the frame pool are closed when the window goes away or Azy stops, and the GPU device is released with them |
| Premiere-compatible | Read-only capture: nothing is posted to Premiere, no input is routed to it, no frame is altered |
| Input-safe | Capture is passive; cursor capture is explicitly switched off, so the pointer is drawn once by the compositor rather than twice |
| Necessary | The brief asks for a *live* mirror; this is the only supported way to get the real window's pixels without screenshots, `BitBlt` or a disk round trip |

Frames never leave the GPU: no staging copy, no readback, no PNG, nothing written to
disk. The capture source is validated against the owning process id before the first
frame and after every re-target, so a recycled window handle cannot make Azy mirror a
different application — and Azy's own window is excluded, so the mirror can never
capture itself (the recursion the brief calls out as critical).

### 4.2 `D3D11` + `DirectComposition` — one surface, premultiplied

The same composition technique as §3.2, with the same guarantees: a flip-model swap
chain, premultiplied alpha, and a layered window carrying `WS_EX_LAYERED |
WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, inserted directly above
Premiere in the z-order and never as a topmost window. The window is bounded by
Premiere's own visible frame, so it can never cover another application or the
taskbar. A layered window composed behind an opaque, maximized window is
indistinguishable from one that is not there, which is why the visibility check
reads a few screen pixels on demand instead of trusting the API return values.

### 4.3 The shader — one pass, no CPU work

The material (surfaces, borders, the panel frames, the accent glow, content
protection) is computed in a single pixel shader over the captured frame. The C++
side uploads one constant buffer per presented frame and nothing else — no per-frame
allocation, no CPU image processing, no readback — and the buffer's layout is
asserted in both languages, because a silent disagreement there would show up only
as a wrong-looking skin. `tools/check-mirror.py` compares the HLSL block, the C++
struct and the `static_assert` member by member (and is itself mutation-tested).

Pacing is event-driven: Windows produces a frame when the source produces one, and
the presenter is a paced timer that is removed entirely while the mirror is hidden —
which is the state Azy sits in whenever Premiere is minimized, hidden or (by
setting) not the foreground window.

## Techniques considered and rejected

| Technique | Why it is not used |
|---|---|
| **DLL injection** (`SetWindowsHookEx` with a DLL, `CreateRemoteThread`, `SetThreadContext`) | Injects code into Premiere, risks its stability and its signed-library checks, is not reversible, and is not necessary for any visual goal. **Never.** |
| **Hooking Premiere's rendering functions** through IAT/inline patching | Undocumented, version-specific, and can desynchronise Premiere's own drawing. Explicitly forbidden by the design brief. |
| **`SetWindowsHookEx(WH_CBT)` process-wide hooks for detection** | Would require code inside Premiere's processes to be useful, and WinEvent hooks already provide everything needed without injection. |
| **Memory patching / resource editing of `Adobe Premiere Pro.exe`** | Modifies Adobe files and breaks updates, signatures and support. Forbidden. |
| **Replacing Premiere's theme files or DLLs** (e.g. shipping `dark` variants) | Same as above: modifies Adobe installation content and cannot be cleanly uninstalled. |
| **A transparent full-screen overlay window** (over the desktop, or over several applications at once) | Would cover windows Azy was not asked to touch, would need hit-test trickery to stay usable, and would cost GPU time continuously. Azy's surfaces are bounded by Premiere's own frame instead. The duplicate window (§3.1) is *not* this technique: it is one window the size of Premiere's visible frame, it is not topmost, and another application brought forward covers it. |
| **`SetWindowCompositionAttribute` acrylic/blur "hacks"** | Undocumented (`user32` private export), historically unstable, interacts badly with DWM, and blurs *behind* a fully opaque client area would be invisible anyway. Rejected even though it is popular in "glass" utilities. |
| **`BitBlt` of Premiere's window to fake translucency** | GDI screen capture, specifically: a CPU bitmap copy per frame, unreliable for GPU-composited windows, wrong on 10-bit/HDR displays, and forbidden by the performance rules. The GPU capture of §3.1 is a different API with none of those properties. |
| **Applying a dark title bar by creating an owner window with `WS_EX_LAYERED` and using it as Premiere's parent** | Reparenting another process's window changes its message routing and z-order semantics. Never safe, never necessary. |
| **`SetWindowLongPtr(GWL_EXSTYLE)` on Premiere's window (e.g. adding `WS_EX_LAYERED` for a translucent Premiere)** | Would alter Premiere's own window semantics and could change input behaviour (a layered window behaves differently for hit-testing and painting). Directly violates the "never break Premiere's controls" rule. |
| **Subclassing Premiere's window procedure with `SetWindowSubclass`/`SetWindowLongPtr(GWLP_WNDPROC)`** | Requires calling into another process's window procedure; cross-process subclassing is not supported by Windows (the call fails or corrupts state), and it is a stability risk even if it worked. |
| **Stamping a custom bitmap over Premiere's title bar via `WM_NCPAINT`** | Requires participating in Premiere's non-client painting → cross-process painting into another application's DC. Rejected. |
| **`WS_EX_NOREDIRECTIONBITMAP` + DirectComposition *per panel*** | One surface per Premiere panel would need one layered window per panel, tracking internal docking geometry through an undocumented hierarchy — the exact "chase Premiere's internal layout" trap the brief warns about. The combination itself is used for **one** surface bounded by Premiere's frame in §3.1/§3.2; what is rejected here is one surface per panel. |
| **Continuous timers / animation / screen capture loops** | Forbidden by the performance requirements. Everything Azy does is event-driven, and the single timer is a 1 s safety net whose normal path does nothing. The duplicate window's cadence is capped at 10/30 fps, is driven by the capture rather than by a clock, and its timer is removed entirely while the window is hidden. |
| **Electron / Chromium / QML / any UI framework** | 50–200 MB of RAM and a rendering process to draw one ring. Rejected in favour of ~550 KB of Win32 + one cached bitmap. |
| **Installing a service or driver** | Not needed for a per-user visual utility; a service would raise the privilege surface, complicate uninstall and (if elevated) interfere with observing a normal user's processes. |

## The fallback ladder (2.0.0: there is nothing to fall back to, by design)

```
capture unsupported / refused ──────────► no skin; state UNSUPPORTED (manual retry)
capture fails 1-3 times      ──────────► no skin; CAPTURE_FAILED, 4 s backoff between attempts
Premiere gone / handle stale ──────────► teardown, GPU released, WAITING_FOR_PREMIERE
minimised / hidden / inactive ─────────► mirror hidden, timer removed, no capture work
```

The round-8 ladder degraded towards a simpler skin: the duplicate window fell back
to the ring and the ring fell back to DWM attributes. 2.0.0 has nothing to degrade
*to* — every one of the deleted layers was a static approximation, and showing a
placeholder over Premiere is exactly what the brief forbids. So Azy draws the mirror
or it draws nothing, and every failure is a named state in the settings window and
the log rather than a fake working skin.

What does not change is the direction. At every step the behaviour gets *less*
involved, never more invasive: there is no path in the code that escalates to a
riskier technique in response to failure, and none that keeps retrying at full rate
after a refusal.
