# Azy Skin — Settings reference

Configuration lives in `%LOCALAPPDATA%\Azy Skin\settings.ini`. It is plain INI,
safe to edit by hand, and Azy reloads it automatically about a second after you
save. Unknown keys are preserved when Azy rewrites the file, so a newer build's
extra settings are not destroyed by an older build.

Log: `%LOCALAPPDATA%\Azy Skin\azy.log` (rotated once at 512 KB to `azy.log.1`).

```ini
[skin]
enabled=1                    ; master ON/OFF (tray: "Skin Enabled")
start_with_windows=0         ; HKCU\...\Run entry pointing at AzySkin.exe --tray
apply_automatically=1        ; skin Premiere as soon as it appears

[appearance]
theme=azy_dark_glass         ; azy_dark_glass | azy_dark | original
glass_intensity=0.55         ; 0.00-1.00  translucency of Azy's own surfaces
border_intensity=0.6         ; 0.00-1.00  hairline border strength
corner_radius=8              ; 0-16 px (DIP) 0 = square
shadow_intensity=0.4         ; 0.00-1.00  soft inner shadow strength
darkness=0.5                 ; 0.00-1.00  charcoal ramp position

[performance]
performance_mode=0           ; static colours only, minimum monitoring
suspend_while_minimized=1
suspend_while_inactive=0     ; opt-in: only skin Premiere when it is foreground

[advanced]
experimental=0               ; opt-in to features that need field verification
safe_mode=0                  ; set automatically after repeated failures
safe_mode_reason=
failures=0                   ; failure-tracker state (persisted across restarts)
failure_window_start=0
failure_last=0
failure_tripped=0
last_premiere_version=       ; last Premiere version seen (diagnostics)

[meta]
schema=1

[extra]
; Round-tripped keys live here, including:
; disabled_features=frame_colors,rounded_frame,frame_backdrop,edge_surface,rounded_surface,shadow,glass
```

---

## Keys

### `[skin]`

| Key | Type | Default | Notes |
|---|---|---|---|
| `enabled` | bool | `1` | Master switch. Off = every Azy visual change removed immediately. Nothing is uninstalled or forgotten; the mode is remembered |
| `start_with_windows` | bool | `0` | Mirrors the `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Azy Skin` value. Set on first install from the installer's choice |
| `apply_automatically` | bool | `1` | When off, Azy still notices Premiere and reports it, but does not apply the skin until you toggle it on from the tray |

### `[appearance]`

| Key | Range | Default | Effect |
|---|---|---|---|
| `theme` | enum | `azy_dark_glass` | `azy_dark_glass` (translucent surfaces), `azy_dark` (opaque), `original` (Azy applies nothing at all) |
| `glass_intensity` | 0–1 | `0.55` | How translucent Azy's own surface is: 0 → 94% opaque, 1 → 78% opaque. Values stay well above "see-through" on purpose: panels must remain as readable as Premiere's own |
| `border_intensity` | 0–1 | `0.60` | Hairline border alpha, 4% → 13% white, plus the top highlight. The cap is deliberate: separation, never an outline |
| `corner_radius` | 0–16 px (DIP) | `8` | Azy's surface radius. Also clamped at 20% of the window's shorter side, and forced to 0 when DWM rounded the frame (the ring then mirrors DWM's own 8 DIP radius instead) |
| `shadow_intensity` | 0–1 | `0.40` | Soft inner shadow at the window band, 10% → 32% black. Ignored in Performance mode |
| `darkness` | 0–1 | `0.50` | Position on the charcoal ramp between "lifted" (46,47,51) and "deep" (11,11,13). Never pure black at either end |

### `[performance]`

| Key | Default | Notes |
|---|---|---|
| `performance_mode` | `0` | Removes translucency and the shadow, disables rounded surface corners and experimental features, and lowers the reaction cadence. Recommended on lower-end machines |
| `suspend_while_minimized` | `1` | Hide the surface and stop all work while Premiere is minimized |
| `suspend_while_inactive` | `0` | Opt-in: only skin Premiere while it is the foreground application. Useful for battery life; the default keeps the window looking consistent when it is behind others |

### `[advanced]`

| Key | Default | Notes |
|---|---|---|
| `experimental` | `0` | Enables features that need field verification on real hardware (currently the Windows 11 Mica backdrop on the window frame). Setting it also clears Safe Mode |
| `safe_mode` | `0` | Set to `1` automatically after 3 failures within 5 minutes. While on: dark frame only, no composition surfaces, no experimental features |
| `safe_mode_reason` | | Human-readable reason, shown in Settings |
| `failures`, `failure_window_start`, `failure_last`, `failure_tripped` | | Failure-tracker state, persisted so a build that failed before also starts in Safe Mode next time |
| `last_premiere_version` | | Last detected Premiere version (diagnostics only) |

### `[extra]`

| Key | Example | Notes |
|---|---|---|
| `disabled_features` | `frame_backdrop,shadow` | Comma-separated list of individual features to switch off, overriding the automatic policy. Recognised keys: `frame_colors`, `rounded_frame`, `frame_backdrop`, `edge_surface`, `rounded_surface`, `shadow`, `glass`. Azy writes this itself if a feature is ever disabled from the UI; it is documented here because it is also a useful debugging tool (e.g. `disabled_features=edge_surface` isolates the DWM layer) |

Any other key you add is preserved verbatim and shown back to you in `[extra]` on
the next save.

---

## Command line

| Flag | Effect |
|---|---|
| `--tray` | Start minimised to the tray (used by the Run entry; the default behaviour is the same, the flag is explicit so the startup path is obvious) |
| `--settings` | Open the settings window at startup |
| `--reset` | Reset the configuration to defaults at startup (keeps `start_with_windows`) |
| `--debug` | Verbose logging (`DEBUG` lines: applied/resolved state, DWM attribute rejections, move/size loop tracking) |
| `--no-tray` | Run without a tray icon (diagnostics: useful when investigating shell interaction) |
| `--help` | Show a short summary and exit |

## Files Azy owns

| Path | Purpose |
|---|---|
| `%LOCALAPPDATA%\Azy Skin\settings.ini` | configuration |
| `%LOCALAPPDATA%\Azy Skin\azy.log` (+ `.1`) | log (rotated once) |
| `%LOCALAPPDATA%\Azy Skin\install-defaults.ini` | one-shot hand-off from the installer; deleted by Azy on first launch |
| `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Azy Skin` | optional startup entry |

Nothing else. Azy writes to no Adobe path, and the uninstaller removes all of the
above.
