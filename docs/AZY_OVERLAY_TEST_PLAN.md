# Azy Skin — duplicate window overlay: test plan

**Everything in this file is written for a machine that can run Windows and
Premiere Pro. Nothing in it has been executed.** The overlay's own status is
**IMPLEMENTED — RUNTIME UNVERIFIED**; the checks that *have* been run are in
`docs/VERIFICATION.md` and are reproduced at the end of this file.

How to use it: run §1 (the first-run checklist) on the real machine, then §3 (the
ten scenarios). One pass through both sections answers every open question listed
in `docs/AZY_OVERLAY_ARCHITECTURE.md` §11. Keep `%LOCALAPPDATA%\Azy Skin\azy.log`
next to the screenshots — it contains one line per state transition, including the
overlay's own, and its absence of lines is as informative as its content.

---

## 0. Before anything: what to look for

The overlay is working when, and only when:

* Azy's icon is in the tray, the status line says **active**, and the *treatment*
  summary mentions the mirror (`Tray → Status`, or the settings window headline);
* the Premiere window on screen is visibly darker, glassy, with 1px separators
  between its panels and a lighter 1px edge;
* the **Program Monitor is not darkened** — the footage looks exactly as it did;
* the mouse works normally (click, drag, wheel, scrub) and the window can be moved
  and resized normally;
* when the skin is switched off, everything returns to normal instantly.

If the window looks identical to before, the duplicate is not on screen. The log
line `duplicate: on screen (mirroring)` is the positive statement; `duplicate: off
(…)` always carries the reason.

---

## 1. First-run checklist

Work through this in order. Each step says what to expect and what to capture if it
does not happen.

**1.1 Install and start** — run `AzySkin-<version>-setup.exe` (or the portable
build), start Azy, then start Premiere Pro.

- Expect: a tray icon. No window of Azy's own anywhere.
- If the installer asks for permission, that is UAC for the install only.

**1.2 The log says what happened** — open `%LOCALAPPDATA%\Azy Skin\azy.log`.

- Expect, in order:
  `overlay: D3D11 device created (feature level …)`,
  `overlay: composition shader compiled for ps_5_0`,
  `overlay: duplicate window ready (click-through, not topmost)`,
  `overlay: mirroring the Premiere window (pid …)`,
  `duplicate: on screen (mirroring)`.
- If `composite shader could not be compiled` or `d3dcompiler` appears: this is
  the shader-compiler path; use Step 2.6 and attach the log.
- If `this host cannot mirror a window`: the capture API is unavailable and the
  ring/veil should be visible instead. **This is the fallback working, not a
  crash** — note the exact text.
- Send the whole log if anything differs.

**1.3 The window is visibly skinned** — look at Premiere.

- Expect: dark charcoal UI, subtle sheen, 1px separators between the panels, a
  lighter 1px frame around the window, corners rounded by ~8px.
- The Program Monitor's picture must be untouched.
- If it looks *exactly* like unskinned Premiere: check the status line says
  `active`, then take a screenshot of the whole window.

**1.4 The mouse belongs to Premiere** — with the skin on:

- click a menu, open it, close it;
- drag a panel divider;
- scrub the timeline;
- right-click in the Project panel;
- click into the Program Monitor.

All of it must behave normally. If a click is ever swallowed, that is a P0 defect:
say which action, and whether Azy was running elevated.

**1.5 Geometry** — in turn:

- move the window by its title bar;
- resize it from a corner;
- maximize it, restore it;
- minimize it (the duplicate must disappear), restore it;
- drag it to a second monitor, if one is attached;
- change the display scaling of that monitor to 125 % and repeat.

Each time: the mirror must follow, and the log may show geometry updates but the
window must not flash off and on.

**1.6 The facts screen** — Settings → *Check visibility*.

Expect a report with three rows: `duplicate`, `overlay`, `ring`. The duplicate row
says either `on screen, … presents, … captured frames` or `not showing: <reason>`.
Copy it into the report — it is the single most useful line for diagnosing a
problem that only happens on that machine.

**1.7 Debug overlay** — Settings → Debug mode on, then *Check visibility*.

A small dark panel appears over Premiere. It should show, in the overlay block:

```
duplicate 0x… | ring 0x… | anchor 0x… (pid …)
duplicate: on screen | capture: running | 1920x1080 -> 1920x1040 | uv …
present … | capture frames … | copies … | empty polls … | failures … | pool resizes …
paced 30 fps (active) | pass-through regions 2 | panel lines 4 | size agrees yes
```

* `size agrees: no` means the capture's pixel size and the window's rectangle
  disagree (a DPI question, see the architecture document §8). Screenshot it.
* `pass-through regions 2` means both monitors were recognised. `0` means the
  panel model could not see them at this window size — screenshot it.
* Two requested fields are deliberately absent: capture latency and GPU memory
  have no per-frame counter in the API used. Nothing is invented for them.

**1.8 Idle cost** — with Premiere sitting still and the duplicate up, look at Task
Manager (Details → CPU, GPU) for about a minute without touching anything.

Expect a small, *stable* number. Nobody has measured it yet; report what you see
rather than a guess. Then press *Suspend skin*: CPU/GPU should return to zero
within a second, and the duplicate should disappear.

**1.9 Clean stop** — *Exit* from the tray (not Suspend).

Expect: Premiere returns to its original appearance immediately, Azy disappears from
the tray, and after a few seconds neither AzySkin.exe nor its worker thread is in
Task Manager. Reopen Premiere: nothing of Azy's is drawn.

---

## 2. Targeted checks

**2.1 Recursive mirror / infinite mirror.** The dangerous case is Azy capturing
something that contains Azy's own window. Check:

- the duplicate, when it is up, must look like Premiere skinned — **not** like a
  screenshot of a screenshot (no progressively darker, nested copies);
- bring another application in front of Premiere: the duplicate must be *covered*
  (it is not topmost), and it must not "climb" above that application;
- maximize Premiere over the whole screen: same check.

If a nested-copy effect ever appears, screenshot it immediately: it means the
capture is not scoped to one window, which the design forbids.

**2.2 Capture border.** On some Windows 10 builds, Windows draws a thin border
around a captured window. Look at the outer 2px of the duplicate: a coloured line
means the border was captured. Note the Windows build (`winver`).

**2.3 Playback.** Play a timeline for ~20 s and scroll a long timeline. Expect the
mirror to look live. Note whether the picture in the Program Monitor keeps up with
the audio, and whether the window ever tears at the top edge.

**2.4 Menus.** Open Premiere's menus, including a submenu. Expect the mirror to
show them. A menu that appears in the real window but not in the mirror (or vice
versa) is exactly the kind of thing that would be a serious limitation: report it.

**2.5 Elevated Premiere.** If you normally run Premiere as administrator, run this
check with Azy non-elevated: the tray tooltip should report `partial` and the log
should explain that Windows blocked placement. Then use Tray → *Restart as
Administrator* and confirm the mirror appears.

**2.6 No shader compiler.** Optional, for completeness: temporarily rename
`C:\Windows\System32\d3dcompiler_47.dll` (do this only on a test machine, and put
it back). Expect: the log says the shader could not be compiled, the duplicate is
not created, and the ring/veil carry the skin.

---

## 3. The ten scenarios

Theoretical simulation: what each scenario exercises, what should happen, and what
to check. "Simulated" means *reasoned through the code and the API contracts*; it
is not evidence, and the column says so.

| # | Scenario | Expected behaviour | What to check |
|---|---|---|---|
| 1 | **Normal use** — Premiere open, skin on, user edits | Duplicate above Premiere, skinned, Program Monitor untouched, input normal, idle pacing 10 fps | §1.3, §1.4, §1.8 |
| 2 | **Window move/resize** — the window is dragged and resized continuously | Geometry follows on each event, capture burst during the drag, no renderer rebuild, no flash | §1.5; log shows geometry updates but no repeated `duplicate window ready` |
| 3 | **Maximize → restore** | Maximized uses the work area (not the overhanging frame); restore returns to the window rectangle | §1.5; debug row `uv` differs between the two states |
| 4 | **Minimize → restore** | Duplicate hides, capture stops (not an error); on restore both come back | §1.5; log: `duplicate: off (minimized)` then `duplicate: on screen` |
| 5 | **Playback** — a sequence is played, then paused | 30 fps while frames arrive, back to 10 fps ~1.5 s after the last one | §2.3; debug row `paced … (active)` → `(idle)` |
| 6 | **Premiere restart** — close Premiere, reopen it | Capture stops, GPU released; the new window is picked up and mirrored without restarting Azy | §1.9 then §1.2 |
| 7 | **Second monitor / DPI change** | Duplicate follows the window to the other monitor at that monitor's DPI, hairlines stay 1 physical pixel | §1.5; debug `size agrees` |
| 8 | **Another app in front** | The duplicate is covered by the other app (it is not topmost) and Azy does nothing about it | §2.1 |
| 9 | **Skin switched off while running** | Suspend: duplicate hides, capture stops, GPU released, Premiere normal. Re-enable: it comes back | §1.8, §1.9 |
| 10 | **Failure of the capture path** | Ring + veil carry the skin, the reason is in the log, no crash, no CPU spike, no Safe Mode | §2.6 (shader) or a Windows build without the capture API |

---

## 4. What has actually been checked (and what has not)

**VERIFIED — `./scripts/verify.sh`, 7/7 steps green on this checkout:**

1. Include hygiene — no file relying on a transitive include.
2. Version strings agree in all 12 files.
3. Overlay contracts (`tools/check-overlay.py`): the shader's constant buffer and
   the C++ `AzyParams` struct have the same members in the same order with the
   same sizes; the region slot counts agree; no `float3` in the buffer; the C++
   `static_assert` equals the size the HLSL packing rules compute (272 bytes); no
   `CreateForMonitor` anywhere in `src/`; the own-process guard is present.
   *The checker was itself tested by breaking the shader in two ways (a renamed
   member, a `float3`) and confirming it failed both times.*
4. Portable core tests: **649 checks, 0 failures**, including the new
   `overlay capture math` group (UV mapping of a maximized window, refusal to draw
   when the geometry does not intersect, panel clipping, pass-through ordering and
   the exclusion of hairlines near the picture regions, shader packing, `overlay_rect`)
   and the `duplicate window style` group (visible defaults, sliders move the look,
   performance mode, rounded corners off, Original theme = no duplicate).
5. Windows cross-compile: the complete application compiles and links with the GUI
   subsystem, including the new capture, compositor and shader modules.
6. Executable inspection: PE32+, x64, GUI subsystem, version/icon/manifest
   resources present, 610,816 bytes (61.1 % of the 1 MB budget).
7. The progress board is in sync with `docs/progress.json`.

**UNVERIFIED — needs the real machine:** everything in §1, §2 and §3 above. In
particular: that `CreateForWindow` succeeds on a Premiere window, that the
hand-declared WinRT ABI behaves as declared at run time, that the mirror lines up
at every DPI, that the mouse really passes through, and any CPU/GPU number at all.

**No claim in this repository says "the overlay works".** The accurate phrasing is
IMPLEMENTED — RUNTIME UNVERIFIED, and that is what the changelog, the README and
the settings window say.
