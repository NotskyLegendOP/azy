Azy Skin 1.3.0 - Windows build
==============================

  AzySkin.exe    64-bit Windows GUI application (no console window)

What it is
----------
A small tray application that skins Adobe Premiere Pro's window from the outside.
Since 1.3.0 it does that by *mirroring* the window: Azy captures Premiere's window
on the GPU, draws the picture back through a small shader - dark charcoal, a little
glass, 1px separators between the panels, a 1px lighter frame, rounded corners - and
shows it in a click-through window placed directly over Premiere. The Program and
Source Monitors are passed through untouched, so footage is never darkened.

Premiere stays the application in every sense: it keeps the mouse, the keyboard, the
focus, the menus, the timeline, the playback and every plugin. Azy decides only what
the pixels look like. It does not touch Premiere: no plugin, no injection, no
patching, no files in an Adobe directory.

On a machine where the GPU path cannot run, the earlier thin "ring" (a 1px hairline,
a 1px raised bezel, a soft inner falloff) plus the translucent sheet carry the skin
instead. That path is unchanged from 1.2.x.

Status of this build
--------------------
The static layers (frame colours, the ring, the sheet) are the ones that have been
shipped and reviewed since 1.0. The duplicate window is new in 1.3.0 and has NOT
been run against a real Premiere on a real machine yet - it is "implemented,
runtime unverified", which is what the documentation says everywhere. If it does not
appear, Premiere is unaffected and the static layers are already doing their job.
docs/AZY_OVERLAY_TEST_PLAN.md is the checklist that closes the remaining questions,
and docs/AZY_OVERLAY_ARCHITECTURE.md explains the design and every open risk.

Running it
----------
1. Double-click AzySkin.exe (no installer, no admin rights needed).
2. It starts in the system tray (no window).
3. Start Premiere Pro; the skin attaches by itself. Left-click the tray icon to
   turn the skin on and off, right-click for the menu, or run it with --settings
   for the settings window.

Checking what is on screen
--------------------------
Settings -> Check visibility. It reports each layer separately: the duplicate window
(with its present count and captured frames), the translucent sheet and the ring. The
duplicate is reported structurally - it is painted by the compositor, so a screen
read is not a trustworthy answer about it - and the report says so.

The log (Settings -> Open log file) carries the same information as lines:
"duplicate: on screen (...)", "overlay capture: started ...", "overlay: duplicate
window ready (click-through, not topmost)".

Notes
-----
* Requires Windows 10 build 17763 (1809) or newer, 64-bit. The duplicate window
  additionally needs the free-threaded capture frame pool from that same build and
  d3dcompiler_47.dll, which ships with Windows.
* Settings live in %LOCALAPPDATA%\Azy Skin\settings.ini and that is the only place
  Azy writes, apart from its log in the same folder.
* This build is a cross-compile (MinGW-based toolchain) of the same sources as the
  MSVC build produced by the windows-msvc CI job; see docs/BUILDING.md.
* Unsigned, like any locally built executable: SmartScreen may ask for
  confirmation the first time.
