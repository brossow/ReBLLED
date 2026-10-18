# Setup page and behaviour redesign (draft)

Status: **prototype for review** (`prototype/setup-v2.html`). Nothing here is in the firmware yet.

## Principles

1. **The door never changes what the lights do.** Opening the door means "I'm here": it wakes
   the lights (restarts the idle timer) and acknowledges a finished print. Nothing else. To
   turn the strip off on purpose there's a visible control.
2. **Every behaviour is a setting, described in plain words where you'd look for it.** No
   gestures, no settings that silently rewrite other settings, no settings that do nothing.
3. **One save model:** every change applies at once and saves itself ("Saved" confirms it).
   There's no Save button to forget.
4. **The page explains itself:** a status line says what the printer is doing and why the
   strip looks the way it does.
5. **Accessible:** every control has a label, choices are real radio groups or lists, focus is
   visible, colour is never the only signal, contrast meets WCAG AA, and it works on a phone.

## Page layout: essentials first, nothing hidden

No simple/expert switch: a mode that hides settings would recreate hidden behaviour (change
something in Expert, switch back, and the strip does things nothing on screen explains).
Instead:

- **Always visible:** Mode (Follow the printer / Always on / Off), **Look** (a preset that
  sets every state's colour and rhythm: *Status colours*, *White, status by rhythm*; it shows
  *Custom* once a state is edited by hand) and Brightness.
- **Everything else folded**, each section with a one-line summary of its current values and
  a **Changed** marker when it differs from the default, so a folded section never hides a
  surprise. The page remembers which sections each person keeps open.
- **Tools** (colour test, colour cycle, Wi-Fi strength) are temporary: not modes, not saved.
  While one runs, the status bar shows **Stop**, which returns to the normal mode.

## Settings model (v2)

| Area | Setting | Values | Replaces |
|---|---|---|---|
| Lights | `mode` | `follow` (follow the printer), `on` (always on), `off` | five mutually exclusive switches |
| Lights | `look` | `status`, `white`, `custom` (a preset over the per-state looks) | new |
| Tools (not saved) | colour test, colour cycle, Wi-Fi strength | run until Stop or a restart | three of the old modes |
| Lights | `brightness` | 0–100 % | same |
| Looks | per state: `printing`, `idle`, `preparing`, `attention`, `error`, `finished` | colour (`warm`, `neutral`, `cool`, `off`, or a custom RGB) + rhythm (`steady`, `pulse`, `slow`, `blink`) | ~15 RGB+WW+CW triplets, statusAnimations |
| Looks (advanced) | per-stage overrides under *preparing* (nozzle cleaning, bed levelling, extrusion calibration, bed scan, first-layer inspection, lidar calibration) and per-error overrides under *error* (filament runout, front cover, nozzle temp, bed temp, serious HMS, fatal HMS) | colour, or "same as group" | the per-stage/per-error triplets |
| Follow the printer | `followChamberLight` | on/off: the strip is off whenever the printer's own light is off | "Replicate State" |
| Timing | `idleOffMinutes` | 0 (never) or minutes; never during a print | inactivity timeout (+ its door-close coupling, removed) |
| Timing | `finishEnds` + `finishMinutes` | `door`, `timer`, `either` | finish exit |
| Timing | `offlineOffSeconds` | seconds before the strip turns off when the printer is unreachable (was a fixed 30 s) | hidden constant |
| Printer's light | `lightOnAtPrintStart`, `lightOnWhenDoorOpens`, `lightOffWhenIdle`, `lightOffWhenFinishTimerEnds` | on/off each | the single "Control Chamber Light" switch |
| LED strip | `stripType` | `rgbww` (RGB + warm + cool white), `rgbw` (RGB + one white), `rgb`, `wwcw` (warm + cool white only), `w` (single white) | nothing (every colour assumed all five channels) |
| LED strip | `channelMap` | which controller output (R, G, B, WW, CW) drives each strip channel; set by the wiring check | rewiring (red/green swapped on the author's strip, 2026-10-17) |
| Printer | `model` | X1C, X1E, P1S, P1P, A1, A1 mini, X2D (untested), H2D (untested) | "P1 printer" (did nothing in firmware) and "lidar in use" (did nothing) |
| Advanced | `hmsIgnore`, `debug`, `mqttLog` | | same |

The firmware migrates a v1 config to v2 on first boot. Every v1 value has a v2 home; the dead
settings are dropped.

## LED strip and wiring check

The controller has five outputs, labelled R, G, B, WW and CW on the board. Strips use some of
them, and the wires aren't always in that order.

- **A single white** is driven on both WW and CW until the wiring check says which it's on,
  so an RGBW strip works out of the box whichever white output it uses.
- **Wiring check:** the page lights one output at a time and asks "What do you see?" (red,
  green, blue, white, warm white, cool white, nothing). The answers set `channelMap` and
  suggest `stripType`; both can be changed by hand.
- **Colour choices follow the strip:**
  - `rgbww`: warm, neutral and cool white, custom colour, off.
  - `rgbw`: white (the white channel), warm or cool white (white mixed with a little RGB), custom, off.
  - `rgb`: white, warm and cool white mixed from RGB, custom, off.
  - `wwcw`: warm, neutral and cool white, off. There are no colours, so states are told apart
    by rhythm.
  - `w`: white or off, with states told apart by rhythm.
- **Saved colours are never rewritten** when the type changes. A choice the strip can't show
  (custom red on a white-only strip) is shown as the nearest it can, and the row says so.
- **Firmware:** one mapping step in the function that writes the PWM (logical channel → output),
  plus white mixing for `rgb`. Host tests cover each type.

## Behaviour rules

- **Priority** (highest first): Lights mode `off`/`on`/`test`/`cycle`/`wifi` → printer
  unreachable → error → needs attention → finished (until it ends) → preparing → printing →
  idle → idle-off.
- **Door open or close:** restarts the idle timer, relights the strip if idle-off, and
  acknowledges *finished* when the finish ends on the door. That's all.
- **Start-up and connection colours** (shown before the printer is reachable) are listed in
  Help, not hidden: white = starting, red = loading settings, orange = joining Wi-Fi,
  pink = no Wi-Fi configured (setup hotspot `BLLED_AP` is on), blue = Wi-Fi connected,
  cyan = connecting to the printer, dim red = printer rejected the access code.
- **The status line** always states a reason, e.g. "Off: idle for 10 min, open the door to
  wake", "Green, slow pulse: print finished, waiting for the door".

## Open questions for review

- The author's strip is RGBW (2026-10-17): which output is its white wired to? The wiring check
  answers this.
- Autosave every change, or live preview plus an explicit Save?
- Should `off` stay off until changed, or until the next print starts?
- Model list: which models matter beyond the X1C and the coming X2D?

## Language

All page text goes in one string table. US English is the default (the original page uses US
spelling); the page switches to British English (colour, grey, …) when the browser's language
is `en-GB`, and the table leaves room for other languages later. The prototype isn't localised
yet.
