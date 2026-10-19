# To do

## Next up

- **Local API + Hubitat driver.** JSON endpoints for printer stage, connection and LED state,
  plus LED on/off and mode (behind the WebUI login), and a Hubitat driver that uses them (for
  example, strip on with the room lights, or announce "print finished"). Small on the firmware
  side.
- **Homepage dashboard tile.** Printer stage, connected and uptime, via Homepage's custom-API
  widget. Nearly free once the API exists.
- **Near-finish cue.** A gentle pulse in the last few percent of a print. Needs
  `mc_percent` added to the MQTT filter in `ParseCallback()`.
- **Preparing defaults vs the X1C camera.** The camera sets its exposure once, at print
  start; with the strip off then, it locks on a dark chamber and overexposes once the strip
  comes on. Fixed on the author's X1C by settings alone (white for "Cleaning the nozzle" and
  "Leveling the bed", lidar steps off; see TESTING.md). Decide whether those two should
  default to the printing colour instead of "same as Preparing" (off) and dim blue
  (`looks()` in `settings.h`). If it ever comes back, the fallback is making the camera
  re-meter by switching resolution over MQTT, as Bambu Studio does:
  `{"camera":{"command":"ipcam_resolution_set","sequence_id":"N","resolution":"720p"}}`, then
  `"1080p"`. That probably restarts the print recording, so test it first.

## Watch-outs

- **USB migration** to the larger partition layout (`docs/USB-MIGRATION.md`). Not urgent:
  builds still fit the old 1.25 MB slot, and every build says so.
- **Bambu firmware updates.** Bambu has been restricting third-party LAN access in recent
  firmware (its "Authorization Control"). Before updating the X1C, check that LAN MQTT still
  works for this controller.
- **X2D.** Its MQTT messages may differ (stage codes, door bit, dual-nozzle HMS). When it
  arrives, capture its messages with *MQTT debug* on before relying on the controller.
- **Upstream.** Offer the security fixes to DutchDevelop/BLLEDController as pull requests:
  everyone on 2.3.0 has those gaps, and the licence is share-alike.
- **Setup page contrast.** The "off" switch track (#ccc on white) is about 1.6:1, below the
  3:1 non-text contrast guideline (WCAG 1.4.11). The knob position still shows the state, but
  a darker track would be clearer.
