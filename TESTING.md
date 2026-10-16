# On-device test checklist

For a new fork build (`2.3.0-br.N`). Everything below needs the real controller, LED strip and printer; it can't be checked off-device.

## Before flashing

- [ ] USB cable (data, not charge-only) and a way to reach the board, in case the new build doesn't boot.
- [ ] Download a config backup: Backup/Restore page → **Download Config**. Keep it off the device; it contains your WiFi password and printer access code.
- [ ] Keep the official `BLLC_V2.3.0.bin` (USB, full flash) and `BLLC_V2.3.0.bin.ota` from the [2.3.0 release](https://github.com/DutchDevelop/BLLEDController/releases/tag/Release_V_2.3.0) for rollback.
- [ ] Open the Web Serial log in a second tab before uploading so you can watch the reboot.

## Flash

- [ ] Firmware Update page → upload `.firmware/BLLC_V2.3.0-br.N.bin.ota`.
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

## If something goes wrong

- Forgot the WebUI password: run the USB setup again (the web flasher's WiFi setup step). Setup over USB clears the WebUI login.

- Device boots but misbehaves: upload the official `BLLC_V2.3.0.bin.ota` on the Firmware Update page. The config format is unchanged.
- Device doesn't come back: flash `BLLC_V2.3.0.bin` over USB with the [web flasher](https://dutchdevelop.github.io/blledsetup/), then restore the config backup.
