# Setup page and behaviour redesign (draft)

Status: **built on `main`, not yet tested on a device.** Settings and migration (stage 1), the LED
rules (2), the web API (3), the setup page (4, `src/www/setupPage.html`) and model detection,
serial lookup and the printer certificate check (5) are in. Stage 6 is the device test. The clickable prototype stays in
`prototype/setup-v2.html`.

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

- **Always visible:** Mode (Follow the printer / Always on / Off), **Look** and Brightness.
  Look is *Standard colours*, *White only* or *Custom*, always all three, with a one-line
  description under the list. Standard and White only are fixed presets; while one is chosen,
  the Status colours rows are shown locked, with a **Customise** button, so editing never
  silently switches the Look. **Custom keeps its own saved set** (starting as a copy of
  Standard), so switching to a preset and back loses nothing. Choosing Custom from the list
  opens Status colours and moves focus into it; loading a page with Custom already chosen
  doesn't.
- **Everything else folded**, each section with a one-line summary of its current values and
  a **Changed** marker when it differs from the default, so a folded section never hides a
  surprise. The page remembers which sections each person keeps open.
- **Tools** (colour test, colour cycle, Wi-Fi strength) are temporary: not modes, not saved.
  While one runs, the status bar shows **Stop**, which returns to the normal mode.

## Settings model (v2)

| Area | Setting | Values | Replaces |
|---|---|---|---|
| Lights | `mode` | `follow` (follow the printer), `on` (always on), `off` | five mutually exclusive switches |
| Lights | `look` + `customLooks` | `standard`, `white`, `custom`; custom keeps its own saved set | new |
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
| Printer | `model` | `auto` (default: use the model the printer reports in its network discovery message) or a model chosen by hand: X1, X1 Carbon, X1E, P1P, P1S, P2S, A1, A1 mini, H2D, H2S, X2D, Not listed. Only the X1 Carbon is tested; the rest say so. | "P1 printer" (did nothing in firmware) and "lidar in use" (did nothing) |
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

## Decided (2026-10-17)

- **Autosave:** every change applies at once and saves itself, with a brief "Saved" message.
- **Off** lasts until you choose another mode.
- **Model:** detected from the printer (its discovery message carries a model code; confirm
  the field against the X1C when building), with a manual override. A hand-kept list goes
  stale as Bambu releases printers.
- **Wiring check:** kept.
- **Section name:** "Status colours" (was "How each printer state looks").

## First-run setup (prototyped)

A guided setup in a modal dialog: opens by itself on a controller with no settings, and again
from **Run setup again** under More.

1. **Wi-Fi and printer connection** (replaces the separate Wi-Fi page for first setup):
   network, password, the printer found on the network (or enter its address), access code.
2. **Your printer:** the detected model, with an override.
3. **LED strip:** type, plus the optional wiring check (same check as on the main page, with
   a small strip preview inside the dialog).
4. **How it looks:** Look and brightness.

**Choosing the printer:** step 1 lists every printer the controller has heard announce itself
(up to 10), each with its model, the name given to it in Bambu's app (if the announcement
carries it; to confirm) and the end of its serial. One controller follows one printer; it's
remembered by serial, so a changed IP is followed. If none are found (a printer on another
network or VLAN), manual entry is preselected. **Typing an IP looks up the serial**: the
controller connects to the printer's MQTT port and reads the name on its TLS certificate, which
is the serial (seen on the author's X1C; `WiFiClientSecure::getPeerCertificate()` exists in core
2.0.17). No access code is needed for that. If nothing answers, the serial is typed by hand.
The same certificate can be remembered and checked on later connections (pinning), closing the
SSDP-spoofing gap properly.

Each step saves when you move on (the page's autosave model). **Finish later** keeps what's
been chosen so far; Esc does the same. Errors are written next to the field and focus moves to
the first one; each step's heading takes focus as you move. Everything else stays on the main
page.

## Web API (stage 3, `src/blled/api.h`)

All endpoints need the WebUI login when one is set. Bodies and replies are JSON.

| Endpoint | Does |
| --- | --- |
| `GET /api/settings` | The v2 settings. |
| `POST /api/settings` | Partial update: only the keys sent change; objects merge (`{"custom":{"error":{"rhythm":"pulse"}}}`). Bad values fall back or clamp. Replies with the settings now in effect. Applies at once; saved 1.5 s after changes stop (and before any restart). |
| `GET /api/status` | What the strip shows and why: `reason` (`idle`, `idle_off`, `finished`, `error`, …), `show`, `state`/`detail`, `colour`, `rhythm`, `outputs`, `tool`, `check`, countdowns (`idleOffIn`, `finishEndsIn`, `idleFor`, seconds) and `printer` (connected, door, light, model, …). The page turns these into the status sentence in its own language. The same object is pushed every second on `/ws` as `lights`. |
| `POST /api/tool` | `{"tool":"test","colour":{…}}`, `{"tool":"cycle"}`, `{"tool":"wifi"}`, `{"tool":"none"}` (Stop). Never saved. |
| `POST /api/check` | `{"output":"R"}` … `"CW"` lights just that output; `{"output":null}` ends the check. The answer is saved with `POST /api/settings` (`wiring`). |
| `GET /api/printers` | Printers heard on the network: IP, serial, `model`, `name` (from Bambu's app), which one is followed. |
| `GET /api/lookup?ip=` | Setup's "Look up": 202 `{"looking":true}` while the controller connects; then 200 with `answered`, `trusted` (signed by Bambu Lab) and `serial`. |

Changes from the page never fade the LEDs on the web server's task (a fade takes 500 ms);
they're shown by the main loop on its next pass. A config restore or factory reset cancels
any pending autosave so it can't overwrite the new file.

## Setup page (stage 4, `src/www/setupPage.html`)

Built from the prototype, minus the simulation panel, talking to the API above. Differences from
the prototype, and why:

- **Step 1 doesn't set up Wi-Fi.** The page is only reachable once the controller is on your
  network, so step 1 shows the network it's on (with a link to the Wi-Fi page) and sets the
  printer. A brand-new controller still joins Wi-Fi through the `BLLED_AP` hotspot's page first;
  the setup dialog then opens the first time the main page loads.
- **Changing the printer restarts the controller** (`POST /api/printer`), which is how it connects
  to the new one. The dialog waits for it to come back and carries on at step 2. It doesn't use
  `/submitWiFi`, which would also clear the WebUI login.
- **"Finish later" also marks setup done**, so the dialog doesn't reopen on every visit. "Run
  setup again" is always there.
- **Preview** uses the colour test with the state's rhythm for 5 seconds (`seconds` in
  `POST /api/tool`), so it ends even if the page is closed.
- **Colour cycle** is disabled on strips without colours (the firmware cycles red, green and blue).
- **Printer without a door sensor:** door-only options are hidden, not greyed out (the prototype
  greyed them): the finish just ends after the set minutes, and "turn the printer's light on when
  the door opens" and the door help aren't shown. Saved choices are left alone, so they come back
  with a model that has a door. As a safety net (a backup restored from another printer, a model
  detected after the setting was saved), the firmware treats "door" as the timer on such a printer.
- **Language:** written in US English; British spelling is applied when the browser's first
  language is en-GB, en-AU, en-NZ, en-IE, en-ZA or en-IN.
- Removed endpoints: `/getConfig`, `/submitConfig`, `/setBrightness`. Added: `POST /api/printer`,
  `POST /api/wifi-rescan`; `/api/status` also gives `firmware`, `rssi` and `preview`.

## Printer model, serial lookup and certificate check (stage 5)

- **Model and name** come from the printer's discovery message (`DevModel.bambu.com`,
  `DevName.bambu.com`, `src/blled/printerModels.h`). Codes confirmed in public integrations map to
  the page's models (`3DPrinter-X1-Carbon` → X1C, `3DPrinter-X1`, `C13` X1E, `C11` P1P, `C12` P1S,
  `N1` A1 mini, `N2S` A1, `O1D` H2D); other codes pass through as they are, and the page treats
  them as unknown (every option shown, lidar and door sensor assumed). Add codes as they're
  confirmed on real printers.
- **The certificate check** (`src/blled/printerCert.h`). The author's X1 Carbon presents a certificate
  issued to its serial and signed by Bambu Lab's own authority, "BBL CA" (valid 2022-2032;
  checked 2026-10-18). The controller embeds that public CA certificate and, before sending the
  access code, opens TLS itself and checks both: signed by BBL CA, issued to the configured serial.
  Dates are ignored (the controller has no clock). If the check fails the code isn't sent, the
  strip shows dim red and the status says "couldn't prove it's your printer". This also closes
  the gap where anything on the network could answer discovery with the printer's serial and
  collect the code when the controller followed an IP change. Advanced → "Check that it's really
  your printer" turns it off for a printer that fails (other models may use another authority).
- **Look up** connects to the typed address the same way and reads the serial from the
  certificate, saying whether it's signed by Bambu Lab. The MQTT task does it (it owns TLS); the
  web server only asks.
- Tests: `test_ssdp` (parser), `test_mqtt` (the code is never sent to an unverified printer), and
  `test_cert` against real mbedtls with generated certificates (`pio test -e native_tls`, needs
  `brew install mbedtls`; separate so the other tests don't).

## Independent review before the device test (stage 6, 2026-10-18)

No blockers. Fixed before flashing:

- **The access code could still go unchecked, in a race:** if the checked TLS connection dropped
  before the login, PubSubClient opened its own, unchecked one. The MQTT client is now a
  `GuardedTlsClient` that refuses to connect unless the certificate check allowed it.
- **Closing file descriptor 0:** in core 2.0.17, `WiFiClientSecure::stop()` leaves the socket
  number at 0, so a second stop (or the destructor) closed descriptor 0, which can by then be an
  open config file. `GuardedTlsClient::stop()` leaves it at -1.
- **Web server task stack** 4 KB → 8 KB (autosave and printer changes now run more there).
- Saving takes the lock, is refused after a restore or factory reset (`configFrozen`), and writes a
  temporary file that replaces the config only when complete. A config that won't parse is kept
  as `blledconfig.unreadable.json` instead of deleted.
- Look up: 10 s TLS handshake limit (was 120 s) on the probe and the MQTT connection, and the
  page waits up to ~30 s for an answer.
- Serials are upper-cased (the certificate name and MQTT topic are case-sensitive).
- A patch can't replace a section with a plain value (`{"custom":"x"}` used to reset it).
- A v1 idle timeout under a minute migrates as 1 minute, not "never".
- No fades on the MQTT task: printer reports are shown by the loop's next pass.
- Cyan "connecting to the printer" now shows until the first connection after boot (then off
  after the unreachable delay), as Help says; it was replaced by idle white at once.

Left for later: `/factoryreset` (GET) and `/configrestore` (form post) can be triggered by any
web page you visit when no WebUI login is set (as in br.2; the JSON API needs
`application/json` and is safe); `/ws` status needs no login (no secrets); the red "loading
settings" boot colour uses the board labels (it's shown before the settings that hold the
wiring load).

## Open questions for review

- The author's strip is RGBW (2026-10-17): which output is its white wired to? The wiring check
  answers this.

## Language

All page text goes in one string table. US English is the default (the original page uses US
spelling); the page switches to British English (colour, grey, …) when the browser's language
is `en-GB`, and the table leaves room for other languages later. The prototype isn't localised
yet.
