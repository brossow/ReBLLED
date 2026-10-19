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
- **Go public** (1.0.0 is released privately; the history is scrubbed of personal details).
  When it's time:
  1. Re-run the scrub scan over `main` and the tags (name, home network names, printer serial,
     home addresses, personal email): it must find nothing.
  2. **Delete and recreate `brossow/ReBLLED`** from this clean history, so the pre-scrub
     commits GitHub still keeps as unreachable objects are gone for good. Push `main`, the
     tags (`v1.0.0`, `v2.3.0-br.1`, and DutchDevelop's `Release`, `Release_V_2.3.0`).
     **Before pushing anything, turn Actions off on the new repo**
     (`gh api -X PUT repos/brossow/ReBLLED/actions/permissions -F enabled=false`): the
     `v1.0.0` tag still contains DutchDevelop's flasher workflows (removed from `main` later),
     and a release runs the workflows at its tag, so they would fire on step 3 and try to push
     ReBLLED firmware into DutchDevelop's flasher repos (they fail without the token, and
     each one emails a failure).
  3. **Recreate the v1.0.0 release** from the saved copy (the exact verified files and notes,
     kept outside the repo with the other controller backups), not from a rebuild: every
     build embeds its compile time, so a rebuild wouldn't match the published checksums.
  4. Make it public, check the About link on the setup page resolves, then announce it (for
     example the Bambu forum's BLLED thread).

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
- **Upstream.** Done: the security fixes went upstream as
  DutchDevelop/BLLEDController#83 (merged; no upstream release yet). Not
  included there: the printer certificate check (offered as a follow-up PR if they want it).
