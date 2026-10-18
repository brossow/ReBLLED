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

- [ ] **Door quick-tap is off by default:** a quick open-and-close does nothing special (the strip just stays on).
- [ ] **Door quick-tap (opt-in):** set "Door Quick Open/Close Toggles LEDs" to 4 and save. With the strip on, open and close the door within ~4 s: the strip goes **off and stays off**. Tap again: it comes **back on** and stays on (no switch-off a moment later). A normal, slower open still turns it on. While the "finished" colour shows, any door use (quick or slow) means "seen it": back to white, never off.
- [ ] Set it back to 0 afterwards: a quick tap no longer toggles.
- [ ] **Finish ending:** after a print, try each of *door*, *timer* and *whichever comes first* (a short timer, like 1 minute, makes this quick).
- [ ] **Rhythms:** finished = slow green pulse; a pause = blue pulse; an error = red blink (an easy one: pause the print, or pull the filament for a runout). Turning "Blink Errors, Pulse Pause / Finish" off makes them steady.
- [ ] **Live brightness:** drag the slider and the strip follows while you drag; reload the page and the new value has stuck.
- [ ] **LED mode:** choosing Color Test or RGB Cycle and then Replicate State works with one click each; only one is ever selected.
- [ ] **Clickable labels:** clicking the words beside a switch toggles it.

## v2 redesign (`2.3.0-br.3`)

The settings file changes format on the first boot (the old one is kept on the device as
`/blledconfig.v1.json`), so the backup below matters more than usual.

### Before flashing

- [ ] **Download a config backup** (Back up and restore → Download). It's your way back to br.2
      with every setting intact.
- [ ] USB cable and access to the board, in case it doesn't come back.
- [ ] Web Serial log open in a second tab to watch the boot.

### Flash and first look

- [ ] `tools/flash.sh <device-ip>`; it reports `2.3.0-br.3` once the device is back.
- [ ] The setup page is the new one; the bottom says **Firmware 2.3.0-br.3**.
- [ ] The setup dialog does **not** open (your migrated settings count as set up).
- [ ] Web Serial shows `[MQTT] connected`, and **no** "Access code not sent" line. (This is the
      real test of the certificate check: the X1C's certificate passed on the Mac, but the
      device's TLS library is a different version.)
- [ ] During boot the strip goes cyan while it connects, then to its normal colour.
- [ ] Status bar: "Printer connected", a status sentence, and a strip preview that matches the
      real strip's colour.
- [ ] Your settings came across: Look is **Custom** with your own colours (purple runout, green
      finish); idle and finish timing as before; HMS ignore list kept.

### Your strip and printer

- [ ] **Your LED strip** → choose **RGB + one white**, then **Check the wiring**: answer what
      you see for each output (R red, G green, B blue, WW or CW white, the other nothing). It
      should say "Found: RGB + one white" and which white output it uses. **Use this.**
- [ ] Tools → Color test with red, green, blue and white: each shows true. Stop.
- [ ] **Your printer** says "It reports itself as: X1 Carbon" (can take up to a minute after
      boot). If it shows a code instead, note it: that's a model code to add.
- [ ] **Run setup again** → step 1 lists your printer by the name it has in Bambu's app, marked
      "followed now". Choose "Enter its address by hand", type the printer's IP, **Look up**:
      "Found a Bambu Lab printer … ending ____" with the right last four digits. Then **Finish
      later** (nothing changes).
- [ ] Optional, disruptive: Run setup again with a wrong serial (change the last digit), Next.
      After the restart the status says the printer couldn't prove it's yours, the strip is
      dim red, and the printer's MQTT never sees the access code. Run setup again with the right
      serial to fix it.

### Lights

- [ ] Mode **Off**, **Always on**, **Follow the printer**: each applies at once, says "Saved",
      and the status bar explains it.
- [ ] Brightness: the strip follows while you drag; reload and it stuck.
- [ ] Status colors: **Preview** each row: 5 seconds in that colour and rhythm, then back.
- [ ] Color cycle: smooth, no pauses. Wi-Fi strength: a colour and the dBm in the status. Stop
      returns to normal each time.
- [ ] Door: quick open-and-close does nothing special. After the strip has gone idle (or set the
      idle time to 5 min to wait less), opening the door wakes it.
- [ ] Idle: the status counts down ("8 min left"), then "Off: no activity for 10 min".
- [ ] A print: preparing steps off (or your colours), printing white, finished = slow green pulse
      until the door opens. A pause = blue pulse.
- [ ] Settings stick through a restart (unplug and replug).
- [ ] No freezes: `python3 tools/ws_timing.py <device-ip> 60` → ~60 messages, no gap over 2 s.
- [ ] On a phone: the page fits, nothing scrolls sideways.
- [ ] Stability: after all of the above, Web Serial shows no restart you didn't cause (a crash
      shows as the boot banner reappearing). The web server's task got a bigger stack for the
      autosave; this is where a shortfall would show.

### Going back to br.2

Upload `BLLC_V2.3.0-br.2.bin.ota` (Firmware update page or `tools/flash.sh`), then **restore
the backup you downloaded** (Back up and restore). br.2 reads the Wi-Fi and printer settings
from the new file but not the LED settings: it reads their absence as brightness 0 and no
colours, so without the restore the strip stays dark. The restore is required, not optional.

### Results

| Check | Result |
|---|---|
| Version, new page, no setup dialog | |
| Connected through the certificate check | |
| Settings migrated | |
| Wiring check (RGBW) | |
| Model detected | |
| Look up | |
| Modes, brightness, previews, tools | |
| Door, idle, print colours | |
| Restart keeps settings | |
| No freezes | |

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
