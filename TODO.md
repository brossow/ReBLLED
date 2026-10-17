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
- **Camera exposure at print start.** The X1C camera sets its exposure once, when it starts at
  the beginning of a print, and doesn't re-check, even if the lights change. At that moment
  the strip is off on purpose: the default colours for nozzle cleaning, bed levelling,
  extrusion calibration, bed scan and first-layer inspection are "off" (bed levelling is a dim
  blue) so the lidar isn't disturbed. So the camera probably meters for the chamber light
  alone and overexposes once the strip comes on.
  1. **Experiment first (no code):** set those stage colours to the running white and
     run a print. Check the camera image *and* whether first-layer inspection still behaves.
  2. Only if that doesn't help: have the controller make the camera re-check by switching its
     resolution and back over MQTT (`ipcam_resolution_set`, as documented by the community).
     Unverified on this firmware; first find out what it does to a timelapse or the AI print
     monitoring mid-print. Test on a non-critical print.

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
