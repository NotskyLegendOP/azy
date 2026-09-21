# How Azy Skin works, in brief

Azy is one small native Win32 process (~600 KB, no dependencies, no admin). It
never touches Premiere's code, files or memory, and it never recreates Premiere's
interface. What it does is **mirror the real Premiere window** and draw that mirror
back wearing a skin.

The long version, with the honest caveats, is
[`AZY_MIRROR_ARCHITECTURE.md`](AZY_MIRROR_ARCHITECTURE.md). This is the one-page
version.

## 1. Finding Premiere

- WMI reports when a process named `adobe premiere pro.exe` (or `- beta.exe`) is
  created or deleted. If WMI is unavailable, a toolhelp snapshot is used instead.
- From that process, Azy picks the **editor window**: the largest visible,
  non-cloaked, captioned, maximised top-level window it owns. No path is ever
  hardcoded, and other Adobe apps can never match.
- It reads that window's rectangle, DPI, monitor and state — and nothing else
  about it. The handle is re-validated against the process id before every
  important operation, because Windows recycles handle values.

## 2. Watching it (event-driven)

`SetWinEventHook` delivers foreground changes, move/resize, minimise/restore,
window creation and cloak changes. Each event marks the relevant state dirty; a
low-rate timer exists only to present the mirror, and it does not exist at all
while the mirror is hidden. Azy never polls for position and never captures the
screen.

## 3. The mirror

| Step | What happens |
| --- | --- |
| **Capture** | Windows Graphics Capture is attached to Premiere's window (never the desktop, never another application) and delivers frames as a GPU texture. No CPU copies, no screenshots, no files. |
| **Compose** | One shader pass (`resources/shaders/mirror.hlsl`) rebuilds those pixels as dark glass: panel surfaces, a diffusion of the pixels behind them, 1px re-lit borders, rounded panel frames, an interior shadow, a faint sheen, static grain, and a localised accent glow. |
| **Present** | The result is presented by DirectComposition in a click-through window placed exactly on Premiere's visible frame. |

What the shader is allowed to change is decided by three things: the **panel model**
(rectangles for the header, Project, Effect Controls, the monitors, Effects, the
right column and the Timeline), a **luminance split** inside those rectangles
(surface vs text/controls), and a **content protection** term that lets bright,
strongly coloured pixels — thumbnails, previews, waveforms — escape most of the
treatment. The **Program and Source Monitor pictures are passed through exactly**:
inside those rectangles the shader returns the captured pixel untouched. It is not
per-control segmentation, and §10 of the architecture document lists precisely where
the model can be fooled.

## 4. Why your clicks still reach Premiere

Every Azy window is created with `WS_EX_LAYERED | WS_EX_TRANSPARENT |
WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, verified on the live window before it is
shown, and answers `WM_NCHITTEST` with `HTTRANSPARENT` as a second line of defence.
Azy installs no input hooks, never calls `SetForegroundWindow`, never takes focus,
and never appears in Alt+Tab. `tools/check-mirror.py` fails if any of those styles
or calls disappears from the source.

## 5. Staying in the right place

Windows recycles handle values and re-orders the z-list whenever something is
raised. So: the stored handle is checked against Premiere's process id before Azy
acts on it, and the mirror is placed by inserting it *between* Premiere and
whatever is directly in front of it — not topmost, not floating over other
applications. A maximised window is drawn on the monitor's work area, because that
is what the user can actually see of it; a window dragged half off the screen is
clamped to the part that is on screen. Moves and resizes are followed with a short
faster burst so a drag does not repaint at event rate.

## 6. Stopping cleanly

Premiere closing, the skin being switched off, or Azy exiting all take the same
path: stop the capture, release the swap chain and every GPU object, destroy Azy's
windows, clear the tracked window. Nothing is left running. Because Azy never
changed anything about Premiere, there is nothing to restore — Premiere looks
exactly as it did.

## 7. What it deliberately never does

No injection, no memory patching, no DLL loading into Premiere, no input hooks, no
screen or desktop capture, no Premiere API or plugin, no project or preference
changes, no writing into an Adobe folder, no fake Premiere UI, no placeholder
rectangle while it waits. When it cannot show the mirror it shows **nothing** and
says why: `WAITING_FOR_PREMIERE`, `CAPTURE_FAILED`, `UNSUPPORTED`, `SUSPENDED` or
`MIRROR_ACTIVE` are the only states, and the diagnostics report the one it is in.
