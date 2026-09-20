Azy Skin - Windows build
========================

  AzySkin.exe    64-bit Windows GUI application (no console window)

What it is
----------
A small tray application that makes Adobe Premiere Pro's window edge look cleaner:
a dark frame treatment plus a four-strip "ring" (1px hairline, 1px raised bezel, a
soft inner falloff) drawn by Azy. It does not touch Premiere: no plugin, no
injection, no patching, no files in an Adobe directory.

Running it
----------
1. Double-click AzySkin.exe (no installer, no admin rights needed).
2. It starts in the system tray (no window).
3. Start Premiere Pro; the skin attaches by itself. Left-click the tray icon to
   turn the skin on and off, right-click for the menu, or run it with --settings
   for the settings window.

Checking that the ring is really on screen
------------------------------------------
Settings -> Open log file, and look for the "ring:" line that appears when the
skin attaches. It names the rectangle the ring was drawn on and the strongest
pixel the renderer produced; "strongest pixel alpha" must not be 0, and "above
Premiere" must say yes.

Notes
-----
* Requires Windows 10 build 17763 (1809) or newer, 64-bit.
* Settings live in %LOCALAPPDATA%\Azy Skin\settings.ini and that is the only place
  Azy writes, apart from its log in the same folder.
* This build is a cross-compile (MinGW-based toolchain) of the same sources as the
  MSVC build produced by the windows-msvc CI job; see docs/BUILDING.md.
* Unsigned, like any locally built executable: SmartScreen may ask for
  confirmation the first time.
