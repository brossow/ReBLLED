# One-time USB migration to the larger partition layout

From `2.3.0-br.2`, builds use `min_spiffs.csv`: **1.875 MB per firmware slot** instead of
1.25 MB, and a 128 KB settings area instead of 1.4 MB (the config is about 2 KB). A device
can only switch layouts over USB. After that, over-the-air updates work as before.

**There's no rush.** A build made for the new layout installs over the air on the old one, as
long as it fits in 1.25 MB. Every build says which applies:

```
Firmware 1217536 bytes: fits the old 1.25 MB OTA slot too (93184 bytes spare).
```

or, once that's no longer true:

```
*** WARNING: firmware is ... bytes, more than the old 1.25 MB OTA slot ...
```

On the old layout, an over-the-air upload of a build that's too big is **refused**, and the
device keeps running its current firmware. Nothing breaks; it just won't update until migrated.

## What you need

- A USB data cable (not charge-only) from the controller's ESP32 board to a computer.
- Chrome or Edge (for the browser method), or this Mac with PlatformIO (for the command-line
  method).
- About 10 minutes.

## 1. Back up the settings (before touching anything)

On the controller's setup page: **Backup & Restore → Download Config**. Keep the file
somewhere private. It contains your Wi-Fi password, printer access code and WebUI login.

The USB flash erases everything, settings included. This backup is how they come back.

## 2. Flash over USB (pick one)

**Browser (no tools to install):**

1. Build on the Mac (`pio run -e esp32dev`), or take the files from the release. You need the
   **full image** `.firmware/ReBLLED_V<version>.bin` (4 MB), *not* the `.bin.ota`.
2. Plug the controller into the computer over USB.
3. Open Espressif's web flasher, <https://espressif.github.io/esptool-js/>, and click
   **Connect**. Choose the serial port that appears when the board is plugged in.
4. Click **Erase Flash** and wait for it to finish.
5. Under *Program*, set the flash address to **`0x0`**, choose the `ReBLLED_V<version>.bin`
   file, and click **Program**.
6. When it's done, unplug and replug the board (or press its EN/RST button).

**Command line (this Mac):**

```bash
ls /dev/cu.usb*                                  # find the board's port
pio run -e esp32dev -t erase --upload-port /dev/cu.usbserial-XXXX
pio run -e esp32dev -t upload --upload-port /dev/cu.usbserial-XXXX
```

Erasing first matters. Without it, the old over-the-air bookkeeping can point the bootloader
at a slot that no longer exists in the new layout.

## 3. Restore the settings

After the flash, the controller has no settings, so it starts its open setup hotspot
**`BLLED_AP`**. There's no login yet, since the settings are gone.

1. On a phone or laptop, join **`BLLED_AP`**.
2. Open **`http://192.168.4.1/backuprestore`**.
3. Upload the config file from step 1. The controller checks the file, saves it and
   restarts.
4. It rejoins your Wi-Fi with all its settings, including the WebUI login and printer IP.

Then delete the config file from the phone or laptop you used.

## 4. Check

- The setup page shows the new firmware version and asks for your WebUI login.
- `python3 tools/ws_timing.py <device-ip> 60` reports ~60 messages and the printer connected.
- Your LED settings and colors are as before.

From now on, update over the air with `tools/flash.sh <device-ip>`.

## Going back

Official 2.3.0 (or an earlier fork build) still installs **over the air** after the
migration: the firmware reads the partition table from flash, so it simply runs on the new
layout. Only a return to the *old layout* would need USB again, and there's no reason to.
