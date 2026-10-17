# On-device test checklist

For a new fork build (`2.3.0-br.N`). Everything below needs the real controller, LED strip and printer; it can't be checked off-device.

## Before flashing

- [ ] USB cable (data, not charge-only) and a way to reach the board, in case the new build doesn't boot.
- [ ] Download a config backup: Backup/Restore page → **Download Config**. Keep it off the device; it contains your WiFi password and printer access code.
- [ ] Keep the official `BLLC_V2.3.0.bin` (USB, full flash) and `BLLC_V2.3.0.bin.ota` from the [2.3.0 release](https://github.com/DutchDevelop/BLLEDController/releases/tag/Release_V_2.3.0) for rollback.
- [ ] Open the Web Serial log in a second tab before uploading so you can watch the reboot.

## Flash

- [ ] `tools/flash.sh <device-ip>` (or Firmware Update page → upload `.firmware/BLLC_V2.3.0-br.N.bin.ota`).
- [ ] The page says "Upload complete. Rebooting...", and after the reboot the setup page shows **Firmware Version: 2.3.0-br.N**.
- [ ] Your settings survived (colours, timers, printer IP/serial).
- [ ] Web Serial log shows `[MQTT] connected` and the LEDs follow the printer.

## Security (set a WebUI login first)

On the WiFi setup page, enter a WebUI username and password and save (the device restarts).

- [ ] In a private browser window, `http://<device-ip>/config.json` asks for a login.
- [ ] After logging in, `/config.json` shows `hasPass`, `hasAccessCode`, `hasWebPass` and **no** password/access code values.
- [ ] WiFi setup page: password and access-code fields are empty with a "(saved - leave blank to keep)" placeholder. Saving without touching them keeps the device connected and the printer working.
- [ ] The network list loads after a short "scanning" wait, and network names display as plain text.
- [ ] Web Serial log page asks for the login and still shows live log lines, in **both Chrome and Safari**. (The log stream is a WebSocket; browsers can't prompt for a login on one, so they must reuse the page's login on their own. If Safari connects but shows no lines, that's this.)
- [ ] Changing to a different WiFi network with the password left blank is refused with "Enter the WiFi password for the new network."
- [ ] Optional: from a terminal, an upload without a login is refused (returns 401) and the device does **not** restart:
      `curl -s -o /dev/null -w "%{http_code}\n" -F "file=@/dev/null" http://<device-ip>/update`

## Reliability

- [ ] **Power-cut recovery:** unplug the router (or turn off the 2.4 GHz WiFi), power-cycle the controller, wait until the `BLLED_AP` hotspot appears (LEDs stay orange), then bring the network back. Within ~1–2 minutes it should restart and reconnect without anyone touching it. While it's still in AP mode, joining `BLLED_AP` should open the setup page (captive portal), and its network list should load (it may take up to ~15 s while a reconnect attempt finishes).
- [ ] **No freezes:** `python3 tools/ws_timing.py <device-ip> 60` reports ~60 messages and no gaps over 2 s (2.3.0: ~31 messages, a ~6 s gap every ~10 s). With RGB cycle mode on, the colour moves smoothly with no pauses.
- [ ] **MQTT back-off:** temporarily enter a wrong access code. Web Serial shows `next attempt in 3 s`, then 6, 12, 24, 30 s (not continuous retries). Put the right code back; it reconnects.
- [ ] Printer list (WiFi setup page → printer search) still finds the printer.

## LEDs

- [ ] Colour changes fade over about half a second instead of jumping.
- [ ] RGB cycle shows saturated colours and respects the brightness slider (try 20% vs 100%).
- [ ] A print start, finish (green) and door interaction behave as before.
- [ ] With **Control chamber light** on and **inactivity timeout** off: closing the door mid-print does **not** turn the LEDs or the chamber light off.
- [ ] If an HMS error turns the LEDs red, they return to normal when the error clears, with both debug-logging options off.
- [ ] Inactivity timeout still turns the lights off when idle.

## Added in br.2

- [ ] **Door quick-tap:** with the strip on, open and close the door within ~4 s: the strip goes **off and stays off**. Tap again: it comes **back on** and stays on (no switch-off a moment later). A normal, slower open still turns it on.
- [ ] **Door window setting:** set "Door Quick Open/Close Toggles LEDs" to 0 and save: a quick tap no longer toggles.
- [ ] **Finish ending:** after a print, try each of *door*, *timer* and *whichever comes first* (a short timer, like 1 minute, makes this quick).
- [ ] **Rhythms:** finished = slow green pulse; a pause = blue pulse; an error = red blink (an easy one: pause the print, or pull the filament for a runout). Turning "Blink Errors, Pulse Pause / Finish" off makes them steady.
- [ ] **Live brightness:** drag the slider and the strip follows while you drag; reload the page and the new value has stuck.
- [ ] **LED mode:** choosing Color Test or RGB Cycle and then Replicate State works with one click each; only one is ever selected.
- [ ] **Clickable labels:** clicking the words beside a switch toggles it.

## If something goes wrong

- Forgot the WebUI password: run the USB setup again (the web flasher's WiFi setup step). Setup over USB clears the WebUI login.

- Device boots but misbehaves: upload the official `BLLC_V2.3.0.bin.ota` on the Firmware Update page. The config format is unchanged.
- Device doesn't come back: flash `BLLC_V2.3.0.bin` over USB with the [web flasher](https://dutchdevelop.github.io/blledsetup/), then restore the config backup.

## Results: 2.3.0-br.1 on the author's X1C controller, 2026-10-17

Flashed over the network (upload ~19 s, back in ~13 s); settings carried over unchanged.

| Check | Result |
|---|---|
| Version shown after reboot | ✅ 2.3.0-br.1 |
| Settings survived | ✅ full config identical to the pre-flash backup (except the printer IP, changed on purpose) |
| Secrets no longer exposed | ✅ `/config.json` has only `has*` flags; `/getConfig` has no access code |
| No main-loop freezes | ✅ 61 status pushes/min, max gap 1.0 s (2.3.0: 31/min, ~6 s gap every 10 s); also 75/75 s in RGB-cycle mode |
| MQTT back-off | ✅ `Connection Failed (-2)` → `next attempt in 30 s` while the printer was unreachable |
| Printer IP follow (SSDP) | ✅ followed a DHCP change and then a reservation change, each after the 2-minute guard |
| Fades | ✅ smooth ~0.5 s fade in both directions |
| RGB cycle | ✅ saturated colours, smooth, dims with the brightness slider |
| Door open/close | ✅ each event handled within a second (LEDs on, idle timer reset) |
| Door quick-toggle (open+close < 2 s) | ⚠️ works but rarely fires: door state arrives via ~1 s printer reports, so the 2 s window is hard to hit. **Unchanged from 2.3.0.** Candidate fix: widen to ~4 s. |
| Login protects everything | ✅ all 12 pages, `/update`, `/configrestore` and the Web Serial socket return 401 without it; refused uploads cause no restart |
| Web Serial behind the login | ✅ Chrome and Safari |
| Cold boot (unplug/replug) | ✅ back and connected to the printer in ~26 s |
| Power-cut recovery | ✅ Wi-Fi unavailable at boot → `BLLED_AP` up; retried once a minute for ~9 h; rejoined, restarted and reconnected 54 s after Wi-Fi returned |
| HMS colour clears without debug logging | ⏳ not testable on demand; waiting for a real HMS error |
| Print start / finish colours | ⏳ next real print |

Found during testing (hardware, not firmware): the strip's red and green wires were swapped; fixed by rewiring.
