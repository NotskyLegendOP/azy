import pathlib

# ---------------------------------------------------------------- CHANGELOG ---
p = pathlib.Path('/home/user/azy/CHANGELOG.md')
t = p.read_text()
entry = '''## [1.2.1] — 2026-09-20

Debug mode and the panel map: the first half of Phase 5, and the tool that makes
the second half exact instead of guessed.

### Added

* **Debug mode** (Settings → Advanced): draws the panel map Azy currently believes
  in straight over the tracked window - the menu bar and header bands, the left
  column (Project over Effect Controls), the two monitors, the right dock and the
  meter strip, the timeline - each labelled with its size, plus a block of facts:
  window handle and class, client rectangle, DPI and scaling, monitor rectangle and
  the workspace in use. It is click-through like every other Azy surface, so the
  window underneath stays fully usable while it is shown, and it costs nothing
  while it is off (no window is created at all).
* **The panel map** (`core/panel_map`): Premiere's docked panels are not windows,
  so "where is the timeline?" has no API answer. The map is the answer Azy uses -
  a workspace profile of *ratios* (plus DIP heights for the two fixed bands)
  applied to the client rectangle at the window's DPI. Eleven panels, six
  profiles. A rectangle too small to be a panel is reported as unusable rather
  than decorated, so a cramped floating window costs a missing region, never a
  broken layout.
* **UI profile** (Settings → Advanced, `ui_profile` in `settings.ini`): choose the
  layout the model assumes - Auto (Editing), Editing, Color, Audio, Effects or
  Graphics (spec §39's "manual UI profile", and §37's version → profile → rules
  chain with the table in configuration instead of code).

### Notes

* The map is rebuilt only when the geometry it was built from changes: one
  comparison per apply, one log block per real layout change, nothing on a timer.
* 211 new portable-core checks (578 total) cover panel-map adjacency, the fixed
  bands scaling with DPI, the exact agreement between `usable` and the minimum
  size, the degenerate cases (an empty client, a window smaller than one panel, a
  minimum larger than the window), workspace parsing and the INI round-trip of the
  two new keys.
* Debug mode is deliberately *not* gated behind "experimental features": it draws
  a diagnostic picture and changes nothing about the skin, and it is the tool a
  user needs exactly when something is wrong.

'''
old = '## [1.2.0] — 2026-09-20'
assert t.count(old) == 1
p.write_text(t.replace(old, entry + old, 1))

# ---------------------------------------------------------------- version bump --
for path in ['/home/user/azy/CMakeLists.txt', '/home/user/azy/packaging/AzySkin.iss',
             '/home/user/azy/resources/azy_skin.manifest', '/home/user/azy/docs/BUILDING.md',
             '/home/user/azy/.github/workflows/release.yml']:
    f = pathlib.Path(path)
    text = f.read_text()
    assert text.count('1.2.0') == 1, path
    f.write_text(text.replace('1.2.0', '1.2.1'))

rc = pathlib.Path('/home/user/azy/resources/azy_skin.rc')
text = rc.read_text()
assert text.count('1.2.0.0') == 2 and text.count('FILEVERSION 1, 2, 0, 0') == 1
text = text.replace('"1.2.0.0"', '"1.2.1.0"')
text = text.replace('FILEVERSION 1, 2, 0, 0', 'FILEVERSION 1, 2, 1, 0')
text = text.replace('PRODUCTVERSION 1, 2, 0, 0', 'PRODUCTVERSION 1, 2, 1, 0')
rc.write_text(text)

vs = pathlib.Path('/home/user/azy/include/azy/core/version_string.hpp')
text = vs.read_text()
assert text.count('1.2.0') == 1
vs.write_text(text.replace('1.2.0', '1.2.1'))

readme = pathlib.Path('/home/user/azy/README.md')
text = readme.read_text()
assert text.count('Azy Skin 1.2.0') == 1
text = text.replace('Azy Skin 1.2.0', 'Azy Skin 1.2.1')
text = text.replace('AzySkin-1.2.0-setup.exe', 'AzySkin-1.2.1-setup.exe')
readme.write_text(text)

prog = pathlib.Path('/home/user/azy/docs/progress.json')
text = prog.read_text()
assert text.count('"shipped": "1.2.0"') == 1
prog.write_text(text.replace('"shipped": "1.2.0"', '"shipped": "1.2.1"'))
print('changelog + version bump done')
