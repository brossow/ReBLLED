# To do

## Next up

- **Hubitat driver.** The JSON API now exists (v2: `GET /api/status` for printer and LED
  state, `POST /api/settings` for mode and brightness; docs/REDESIGN.md). A driver on top of it
  (strip on with the room lights, announce "print finished"). With a WebUI login set, the driver
  needs it (basic auth) and must send `Content-Type: application/json`.
- **Homepage dashboard tile.** Printer stage, connection and what the strip shows, from
  `/api/status` via Homepage's custom-API widget (basic auth if a login is set).
- **Near-finish cue.** A gentle pulse in the last few percent of a print. Needs
  `mc_percent` added to the MQTT filter in `ParseCallback()`.
- **Name and 1.0.0 release.** Rename (the author's call), version 1.0.0, README for the new
  product, GitHub release, credit DutchDevelop.

## Watch-outs

- **Camera exposure (X1C).** Done in br.7: Standard and White only keep the strip lit while
  the nozzle is cleaned and the bed levelled, off for the lidar steps (the camera sets its
  exposure at print start). If overexposure ever comes back, the fallback is making the camera
  re-meter by switching resolution over MQTT, as Bambu Studio does:
  `{"camera":{"command":"ipcam_resolution_set","sequence_id":"N","resolution":"720p"}}`, then
  `"1080p"`. That probably restarts the print recording, so test it first.

- **USB migration** to the larger partition layout (`docs/USB-MIGRATION.md`). Getting closer:
  br.7 is 1.24 MB of the old 1.25 MB slot (about 67 KB left); every build says so.
- **Bambu firmware updates.** Bambu has been restricting third-party LAN access in recent
  firmware (its "Authorization Control"). Before updating the X1C, check that LAN MQTT still
  works for this controller.
- **X2D.** Its MQTT messages may differ (stage codes, door bit, dual-nozzle HMS). When it
  arrives, capture its messages with *MQTT debug* on before relying on the controller.
- **Upstream.** Offer the security fixes to DutchDevelop/BLLEDController as pull requests:
  everyone on 2.3.0 has those gaps, and the licence is share-alike.
