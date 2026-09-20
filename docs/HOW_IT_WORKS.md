# How Azy Skin works, in brief

Azy is one small native Win32 process (~560 KB, no dependencies, no admin). It
never touches Premiere's code, files or memory. It decorates the window from the
outside, using three independent layers.

## 1. Finding Premiere

- WMI reports when a process named `adobe premiere pro.exe` (or `- beta.exe`) is
  created or deleted. If WMI is unavailable, a toolhelp snapshot is used instead.
- From that process, Azy picks the **editor window**: the largest visible,
  non-cloaked, captioned, maximised top-level window it owns. No path is ever
  hardcoded, and other Adobe apps can never match.
- It reads that window's rectangle, DPI, monitor and state — and nothing else
  about it.

## 2. Watching it (event-driven)

`SetWinEventHook` delivers foreground changes, move/resize, minimise/restore,
window creation and cloak changes. Each event marks the relevant state dirty; a
1 Hz timer exists only as a safety net for a notification Windows failed to
deliver. Azy never polls for position, never captures the screen, and does
nothing at all when nothing changed.

## 3. The three visual layers

| Layer | What it does | How |
| --- | --- | --- |
| **Frame** | Dark title bar, caption/border/text colours, corner rounding, optional backdrop | Documented `DwmSetWindowAttribute` calls on Premiere's own window. Previous values are saved and restored on stop. |
| **Ring** | The 1 px hairline, 1 px lighter bezel and soft inner falloff around the window edge | Four thin click-through layered windows, each with a small premultiplied-ARGB bitmap drawn by GDI+ in frame coordinates. A 1 px line stays exactly 1 px because nothing is scaled. ~78 KB of bitmap at 1080p. |
| **Veil** | The whole-window charcoal tint | One layered window the size of the frame, filled with a single colour and blended by `SetLayeredWindowAttributes` with a constant alpha. No bitmap, no per-pixel work, alpha capped at 0.60 so Premiere can never be hidden. |

The panel map (ratios of the client area plus fixed DIP bands, per workspace
profile) is a *model* of where Premiere's panels are. It drives the debug overlay
today; regional decoration will use the same rectangles.

## 4. Why your clicks still reach Premiere

Every Azy window is created with `WS_EX_LAYERED | WS_EX_TRANSPARENT |
WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW`, verified on the live window before it is
shown, and answers `WM_NCHITTEST` with `HTTRANSPARENT` as a second line of
defence. Azy installs no input hooks, never calls `SetForegroundWindow`, never
takes focus, and never appears in Alt+Tab.

## 5. Staying in the right place

Windows recycles handle values and re-orders the z-list whenever something is
raised. So: the stored handle is checked against Premiere's process id before
Azy ever acts on it, and the surfaces are placed by inserting them *between*
Premiere and whatever is directly in front of it — not topmost, not floating over
other applications — with a self-check after every placement that logs whether
the ring really ended up above Premiere.

## 6. Stopping cleanly

Premiere closing, the skin being switched off, or Azy exiting all take the same
path: restore the DWM attributes, hide and destroy Azy's windows, free the GDI
and GDI+ resources, clear the tracked window. Nothing is left running, and
Premiere looks exactly as it did before.

## 7. What it deliberately never does

No injection, no memory patching, no DLL loading into Premiere, no input hooks,
no screen capture, no Premiere API or plugin, no project or preference changes,
no writing into an Adobe folder, no animation, no fake Premiere UI. It cannot
restyle Premiere's own widgets, and it says so instead of pretending otherwise.
