# Changelog

## 1.0.0: ReBLLED

The first release under the ReBLLED name, built on BLLEDController 2.3.0 by DutchDevelop. Fork
builds `2.3.0-br.3` to `br.7` led up to it; design and decisions in
[docs/REDESIGN.md](docs/REDESIGN.md), on-device results in [TESTING.md](TESTING.md).

- **New setup page:** essentials first, folded sections with summaries, autosave, a four-step
  first-run setup, live status line that says why the strip shows what it shows. All pages
  redone in one design (light and dark, phone, keyboard and screen reader); timestamped backup
  files; Wi-Fi page with explicit "no password" and "require a login" checkboxes.
- **New LED logic:** one rule set decides what the strip shows; status rhythms; Looks
  (Standard, White only, Custom); per-step and per-error colors with previews; the strip lit
  for nozzle cleaning and bed levelling by default (camera exposure), off for lidar steps.
- **No hidden behavior:** the door quick-toggle gesture is gone; each way the controller
  switches the printer's own light is a separate setting; modes Follow / Always on / Off.
- **Strip types and wiring check:** RGB+WW+CW, RGB+W, RGB, WW+CW, W; swapped wires corrected in
  software.
- **Printers:** discovery with model and name (Bambu Studio model IDs), serial lookup by
  address, models without a door sensor or lidar handled.
- **Security:** the access code goes only to a printer whose certificate is issued to its serial
  and signed by Bambu Lab; changes need the controller's own pages (no cross-site requests);
  open networks supported.
- **Settings:** converted from BLLED automatically on first boot (the original kept on the
  device); safer saving (complete-or-nothing, an unreadable file kept aside).
- **ReBLLED's own icon** in browser tabs and bookmarks (a lit LED strip on a dark tile): SVG,
  a 32 px PNG fallback, and a home-screen icon for phones.
- **Under the hood:** a JSON API for the page (and for future integrations), fades off the web
  and MQTT tasks, a second test suite for the certificate check; 129 host tests.

## 2.3.0-br.2 (fork build)

- **Door quick open-and-close toggles the LEDs reliably.** In 2.3.0 a toggle-off was undone within the same printer message, and opening the door relit the strip before the toggle decided, so it could only ever end "off". It now flips the state from before the door opened (except while the "finished" color shows: then door use just acknowledges it), It is now **off by default** (a hidden gesture surprised people) and opt-in via "Door Quick Open/Close Toggles LEDs" (seconds; 2.3.0 always had it at a fixed 2 s, hard to hit because door state arrives in ~1 s printer reports). The planned UI redesign replaces it with a visible lights on/off control.
- **Finish color ends on the door, a timer, or whichever comes first.** Timer mode no longer treats the first minutes after any boot as a finished print.
- **States readable without color.** Errors blink (1 per second, well under the photosensitivity threshold), pause / first-layer / clog pulse, finished pulses slowly; printing and idle stay steady. Can be turned off.
- **Brightness applies live** while dragging the slider and saves when released.
- **Clearer setup page.** The LED modes are one labeled radio group (choosing test color or RGB cycle used to silently switch off "replicate printer light"); the finish ending is a three-way choice; the text beside every switch is clickable and announced by screen readers; switches show keyboard focus.
- **Room to grow.** Built for a partition layout with 1.875 MB firmware slots (was 1.25 MB). Switching a device to it needs one USB flash ([docs/USB-MIGRATION.md](docs/USB-MIGRATION.md)); until then builds still install over the air while they fit the old slot, and every build says whether they do.
- **Tests.** The real LED and MQTT code compiles and runs on a Mac (`pio test -e native`): 54 tests drive printer states and real-shaped MQTT messages and check what the LEDs would do.

## 2.3.0-br.1 (fork build)

- **Login actually protects the device.** When a WebUI username/password is set, every settings, upload, scan and log endpoint requires it (in 2.3.0, `/config.json`, `/submitWiFi`, `/update` and `/configrestore` did not). Stored secrets (WiFi password, printer access code, WebUI password) are never sent back to the browser: on the WiFi setup page, leave those fields blank to keep the saved value, and clear the username to remove the login. Forgot the password? Sending the Wi-Fi setup over USB serial again (the web flasher's setup step) clears the login.
- **Recovers after a power cut.** If the saved WiFi isn't reachable at boot, the device keeps the `BLLED_AP` setup hotspot up (now with a working captive portal) but tries your network for 15 seconds every minute and restarts into normal mode once it's back (2.3.0 stayed in AP mode until power-cycled).
- **No more 5-second freezes.** Printer discovery no longer blocks the main loop for ~5.5 s every 10 s.
- **Gentler on the printer.** MQTT reconnects back off from 3 to 30 seconds instead of retrying continuously.
- **LED fixes.** Color changes actually fade; RGB cycle respects the brightness limit; an HMS error color clears when the error does (even with debug logging off); with the inactivity timeout disabled, closing the door no longer turns the lights off mid-print.
- **Robustness.** A malformed restored config or color value can no longer crash or hang the device at boot; restored backups are validated before they replace the config; the firmware update page reports failures.
- **Thread safety.** LED/printer state and the Web Serial log are protected against concurrent access from the loop, MQTT and web-server tasks; only the MQTT task talks to the printer.
- **Reproducible builds.** Platform and library versions are pinned in `platformio.ini`.
