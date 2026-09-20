# Azy Skin — Techniques, and why they are safe

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

## Level 1 — Native window composition (used)

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

## Level 2 — Targeted lightweight visual surfaces (used)

### 2.1 One click-through layered window (`UpdateLayeredWindow`)

Azy's visual element is a single ring around the window edge, built from four
1px-stroke layers:

| Layer | Purpose |
|---|---|
| 1px hairline on the frame edge | separation: the line that makes panel boundaries read |
| 1px raised bezel just inside it | legibility: a purely *dark* edge is invisible over Premiere's own near-black panels, so the edge is slightly *lighter* than the panel, at 5–17% white |
| soft dark falloff inward across the band | depth: a quadratic shadow, starting one pixel in so it never muddies the bezel |
| subtle translucent wash behind the first few pixels | the "glass" hint: a fade to nothing within 2–6px, never a solid strip |
| 1px top inner highlight | light falling on glass, not a glow |

![The ring, drawn from the renderer's own maths](images/ring-preview.png)

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

## Techniques considered and rejected

| Technique | Why it is not used |
|---|---|
| **DLL injection** (`SetWindowsHookEx` with a DLL, `CreateRemoteThread`, `SetThreadContext`) | Injects code into Premiere, risks its stability and its signed-library checks, is not reversible, and is not necessary for any visual goal. **Never.** |
| **Hooking Premiere's rendering functions** through IAT/inline patching | Undocumented, version-specific, and can desynchronise Premiere's own drawing. Explicitly forbidden by the design brief. |
| **`SetWindowsHookEx(WH_CBT)` process-wide hooks for detection** | Would require code inside Premiere's processes to be useful, and WinEvent hooks already provide everything needed without injection. |
| **Memory patching / resource editing of `Adobe Premiere Pro.exe`** | Modifies Adobe files and breaks updates, signatures and support. Forbidden. |
| **Replacing Premiere's theme files or DLLs** (e.g. shipping `dark` variants) | Same as above: modifies Adobe installation content and cannot be cleanly uninstalled. |
| **A transparent full-screen overlay window** | The brief forbids it: it would cover the whole desktop, block input or need fragile hit-test trickery, and would cost GPU time continuously. Azy's surface is bounded by Premiere's frame instead. |
| **`SetWindowCompositionAttribute` acrylic/blur "hacks"** | Undocumented (`user32` private export), historically unstable, interacts badly with DWM, and blurs *behind* a fully opaque client area would be invisible anyway. Rejected even though it is popular in "glass" utilities. |
| **`BitBlt` of Premiere's window to fake translucency** | Requires screen capture of another process's surface (performance cost, DWM restrictions, content protection), and produces wrong results on 10-bit/HDR displays. |
| **Applying a dark title bar by creating an owner window with `WS_EX_LAYERED` and using it as Premiere's parent** | Reparenting another process's window changes its message routing and z-order semantics. Never safe, never necessary. |
| **`SetWindowLongPtr(GWL_EXSTYLE)` on Premiere's window (e.g. adding `WS_EX_LAYERED` for a translucent Premiere)** | Would alter Premiere's own window semantics and could change input behaviour (a layered window behaves differently for hit-testing and painting). Directly violates the "never break Premiere's controls" rule. |
| **Subclassing Premiere's window procedure with `SetWindowSubclass`/`SetWindowLongPtr(GWLP_WNDPROC)`** | Requires calling into another process's window procedure; cross-process subclassing is not supported by Windows (the call fails or corrupts state), and it is a stability risk even if it worked. |
| **Stamping a custom bitmap over Premiere's title bar via `WM_NCPAINT`** | Requires participating in Premiere's non-client painting → cross-process painting into another application's DC. Rejected. |
| **`WS_EX_NOREDIRECTIONBITMAP` + DirectComposition per-panel surfaces** | Per-panel surfaces would need one layered window per Premiere panel, tracking internal docking geometry through undocumented hierarchy — the exact "chase Premiere's internal layout" trap the brief warns about. Reserved for a future optional module, never a default. |
| **Continuous timers / animation / screen capture loops** | Forbidden by the performance requirements. Everything Azy does is event-driven, and the single timer is a 1s safety net whose normal path does nothing. |
| **Electron / Chromium / QML / any UI framework** | 50–200 MB of RAM and a rendering process to draw one ring. Rejected in favour of ~440 KB of Win32 + one cached bitmap. |
| **Installing a service or driver** | Not needed for a per-user visual utility; a service would raise the privilege surface, complicate uninstall and (if elevated) interfere with observing a normal user's processes. |

## The fallback ladder (how Azy degrades)

```
DWM frame colours       ── rejected? ──► dark frame only (Level 1.1)
dark frame              ── rejected? ──► DWM layout attributes only (rounded corners)
any DWM attribute       ── rejected? ──► Azy's own surface only (Level 2.1)
surface creation fails  ── recurring? ─► Safe Mode: dark frame only, surfaces disabled
3 failures in 5 minutes ──────────────► Safe Mode, persisted across restarts
```

At every step the visual result gets simpler, never more invasive. There is no
path in the code that escalates to a riskier technique in response to failure.
